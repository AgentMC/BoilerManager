namespace BoilerManager.Model
{
    public class BoilerMeta : BoilerMetaBase
    {
        const double FullWarm = 45.0, DefCold = 25, TimeToCool = 45;
        public const int MAX_ENTRIES = 7*24*60;

        private readonly List<(DateTime timestamp, double[] values)> Readings = new();

        public void Add(double[] values)
        {
            if (values.Length != 3) throw new Exception("Wrong count of Entries");
            var timestamp = DateTime.UtcNow;

            Readings.Add((timestamp, values));
            if(Readings.Count > MAX_ENTRIES)
            {
                Readings.RemoveAt(0);
            }

            UpdateStats(timestamp, values);
        }

        private void UpdateStats(DateTime timestamp, double[] values)
        {
            LastNotified = timestamp;
            BoilerWarm = (ClampPercent(values[0]) + ClampPercent(values[1]) + ClampPercent(values[2])) / 3;
            BoilerTime = BoilerWarm * TimeToCool;
        }

        private static double ClampPercent(double value)
        {
            var normalized = Math.Max(Math.Min(value, FullWarm), DefCold);
            return (normalized - DefCold) / (FullWarm - DefCold);
        }

        public BoilerMetaResponse ToResponse(int count = -1)
        {
            var r = new BoilerMetaResponse(LastNotified, BoilerWarm, BoilerTime);
            if (count == -1) count = Readings.Count;
            for (int i = Math.Max(Math.Max(Readings.Count - count, 0), Readings.Count - 1); i > 0 && i < Readings.Count; i++)
            {
                var (timestamp, values) = Readings[i];
                r.Readings[timestamp] = values;
            }
            return r;
        }

        public static readonly BoilerMeta Default = new();
    }
}
