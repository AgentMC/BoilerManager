namespace BoilerManager.Model
{
    public class BoilerMeta : BoilerMetaBase
    {
        public const int MAX_ENTRIES = 7*24*60;
        const double    FullWarm = 50.0,//[°C] Maximum temperature the boiler can be heated as a whole
                        WashCold = 33,  //[°C] Lowest temperature of comfort showering
                        TimeToCool = 27;//[Minutes] When heated to the maximum, how long can you shower?

        private static readonly string PersistanceFile = Path.Combine(Environment.ExpandEnvironmentVariables("%TEMP%"), "BoilerManagerCache.dat");

        private readonly List<(DateTime timestamp, double[] values)> Readings = new();
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
                            Readings.Add((DateTime.Parse(entry[0]), new[] { double.Parse(entry[1], c), double.Parse(entry[2], c), double.Parse(entry[3], c) }));
                        }
                    }
                }
            }

            persisterThread = new Thread(PersisterThread) { IsBackground = true, Name = nameof(PersisterThread), CurrentCulture = c };
            persisterThread.Start();
        }

        public void Add(double[] values)
        {
            if (values.Length != 3) throw new Exception("Wrong count of Entries");
            var timestamp = DateTime.UtcNow;

            block.EnterWriteLock();
            {
                Readings.Add((timestamp, values));
                if (Readings.Count > MAX_ENTRIES)
                {
                    Readings.RemoveAt(0);
                }

                UpdateStats(timestamp, values);
            }
            block.ExitWriteLock();
            goWrite.Set();
        }

        private void UpdateStats(DateTime timestamp, double[] values)
        {
            LastNotified = timestamp;
            BoilerWarm = (ClampPercent(values[0]) + ClampPercent(values[1]) + ClampPercent(values[2])) / 3;
            BoilerTime = BoilerWarm * TimeToCool;
        }

        private static double ClampPercent(double value)
        {
            var normalized = Math.Max(Math.Min(value, FullWarm), WashCold);
            return (normalized - WashCold) / (FullWarm - WashCold);
        }

        public BoilerMetaResponse ToResponse(int count = -1)
        {
            BoilerMetaResponse r;
            block.EnterReadLock();
            {
                r = new BoilerMetaResponse(LastNotified, BoilerWarm, BoilerTime);
                if (count < 0 || count > Readings.Count) count = Readings.Count;
                for (int i = Readings.Count - count; i >= 0 && i < Readings.Count; i++)
                {
                    var (timestamp, values) = Readings[i];
                    r.Readings[timestamp] = values;
                }
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
