namespace BoilerManager.Model
{
    public class BoilerMeta : BoilerMetaBase
    {
        public const int                 MAX_ENTRIES = 7*24*60,    //[Number] The max length of in-memory/file history
                                         TimeToCool = 27;          //[Minutes] When heated to the maximum, how long can you shower?
        private static readonly double[] FullWarm = { 48, 48, 36 },//[°C] Maximum temperature the boiler can be heated, per sensor
                                         WashCold = { 34, 28, 24 },//[°C] Lowest temperature of comfort showering, per sensor
                                         Mul = { 0.15, 0.6, 0.25 };//[Number] Importance factor of every sensor when calculating the data
                        

        private static readonly string PersistanceFile = Path.Combine(Environment.ExpandEnvironmentVariables("%TEMP%"), "BoilerManagerCache.dat");

        private readonly List<(DateTime timestamp, double[] values)> Readings = new();
        private readonly double[] last3 = new double[] { 0, 0, 0 };
        private readonly ReaderWriterLockSlim block = new();
        private readonly AutoResetEvent goWrite = new(false);
        private readonly Thread persisterThread;
        
        public BoilerMeta() 
        {
            var c = System.Globalization.CultureInfo.InvariantCulture;
            if (File.Exists(PersistanceFile))
            {
                using var reader = new StreamReader(PersistanceFile, System.Text.Encoding.UTF8);
                var entry = reader.ReadLine()?.Split('|');
                if(entry?.Length == 3)
                {
                    BoilerTime = double.Parse(entry[0], c);
                    BoilerWarm = double.Parse(entry[1], c);
                    LastNotified = DateTime.Parse(entry[2]);
                    while (!reader.EndOfStream)
                    {
                        entry = reader.ReadLine()?.Split('|');
                        if(entry?.Length == 4)
                        {
                            AddReading(DateTime.Parse(entry[0]), new[] { double.Parse(entry[1], c), double.Parse(entry[2], c), double.Parse(entry[3], c) });
                        }
                    }
                }
            }

            persisterThread = new Thread(PersisterThread) { IsBackground = true, Name = nameof(PersisterThread), CurrentCulture = c };
            persisterThread.Start();
        }

        private void AddReading(DateTime key, double[] values)
        {
            Readings.Add((key, values));
            if (Readings.Count > MAX_ENTRIES)
            {
                Readings.RemoveAt(0);
            }

            last3[0] = last3[1];
            last3[1] = last3[2];
            last3[2] = values[0] + values[1] + values[2];
        }

        public void Add(double[] values)
        {
            if (values.Length != 3) throw new Exception("Wrong count of Entries");
            var timestamp = DateTime.UtcNow;

            block.EnterWriteLock();
            {
                AddReading(timestamp, values);
                UpdateStats(timestamp, values);
            }
            block.ExitWriteLock();
            goWrite.Set();
        }

        private void UpdateStats(DateTime timestamp, double[] values)
        {
            LastNotified = timestamp;
            BoilerWarm = 0;
            for (int i = 0; i < values.Length; i++) 
            {
                var normalized = Math.Max(Math.Min(values[i], FullWarm[i]), WashCold[i]);
                BoilerWarm += (normalized - WashCold[i]) / (FullWarm[i] - WashCold[i]) * Mul[i];
            }
            BoilerTime = BoilerWarm * TimeToCool;
        }

        public BoilerMetaResponse ToResponse(int count = -1)
        {
            BoilerMetaResponse r;
            block.EnterReadLock();
            {
                r = new BoilerMetaResponse(LastNotified, BoilerWarm, BoilerTime);
                if (count < 0 || count > Readings.Count) count = Readings.Count;
                for (int i = Readings.Count - count; i < Readings.Count; i++)
                {
                    var (timestamp, values) = Readings[i];
                    r.Readings[timestamp] = values;
                }
                r.IsHeating = last3[0] < last3[1] && last3[1] < last3[2];
            }
            block.ExitReadLock();
            return r;
        }

        public static readonly BoilerMeta Default = new();

        private void PersisterThread()
        {
            while (true)
            {
                goWrite.WaitOne();
                block.EnterReadLock();
                {
                    using var writer = new StreamWriter(PersistanceFile, System.Text.Encoding.UTF8, new FileStreamOptions { Mode = FileMode.Create, Access = FileAccess.Write, Share = FileShare.ReadWrite | FileShare.Delete });
                    writer.WriteLine($"{BoilerTime}|{BoilerWarm}|{LastNotified:O}");
                    foreach (var (timestamp, values) in Readings)
                    {
                        writer.WriteLine($"{timestamp:O}|{values[0]}|{values[1]}|{values[2]}");
                    }
                }
                block.ExitReadLock();
            }
        }
    }
}
