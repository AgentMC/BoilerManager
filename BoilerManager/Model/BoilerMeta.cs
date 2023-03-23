namespace BoilerManager.Model
{
    public class BoilerMeta
    {
        const double FullWarm = 45.0, DefCold = 25, TimeToCool = 45;

        public double BoilerWarm { get; private set; }
        public double BoilerTime { get; private set; }
        public DateTime LastNotified { get; private set; }

        public Dictionary<DateTime, double[]> Readings { get; init; } = new();

        public void Add(double[] values)
        {
            if (values.Length != 3) throw new Exception("Wrong count of Entries");
            var timestamp = DateTime.UtcNow;
            Readings.Add(timestamp, values);

            UpdateStats(timestamp, values);
            
            if(Readings.Count > 10080)
            {
                var oldestEntry = Readings.Keys.Min();
                Readings.Remove(oldestEntry);
            }
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

        public static readonly BoilerMeta Default = new();
    }
}
