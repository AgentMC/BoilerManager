namespace BoilerManager.Model
{
    public class BoilerMeta
    {
        const double FullWarm = 45.0, DefCold = 25;

        public double BoilerWarm { get; init; }
        public double BoilerTime { get; init; }
        public DateTime LastNotified { get; init; }

        public Dictionary<DateTime, double[]> Readings { get; init; }

        public BoilerMeta(Dictionary<DateTime, double[]> readings)
        {
            Readings = readings;
            LastNotified = readings.Keys.Max();

            var lastEntry = Readings[LastNotified];
            if (lastEntry.Length != 3) throw new Exception("Wrong count of Entries");
            BoilerWarm = (ClampPercent(lastEntry[0]) + ClampPercent(lastEntry[1]) + ClampPercent(lastEntry[2])) / 3;
            BoilerTime = BoilerWarm * 45;
        }

        private static double ClampPercent(double value)
        {
            var normalized = Math.Max(Math.Min(value, FullWarm), DefCold);
            return (normalized - DefCold) / (FullWarm - DefCold);
        }


        public static readonly BoilerMeta Default = new(
        new Dictionary<DateTime, double[]>()
        {
            {DateTime.UtcNow.AddMinutes(-9), new double[]{20,20,20 } },
            {DateTime.UtcNow.AddMinutes(-8), new double[]{25,20,20 } },
            {DateTime.UtcNow.AddMinutes(-7), new double[]{30,20,20 } },
            {DateTime.UtcNow.AddMinutes(-6), new double[]{35,25,20 } },
            {DateTime.UtcNow.AddMinutes(-5), new double[]{40,30,20 } },
            {DateTime.UtcNow.AddMinutes(-4), new double[]{45,30,20 } },
            {DateTime.UtcNow.AddMinutes(-3), new double[]{50,35,20 } },
            {DateTime.UtcNow.AddMinutes(-2), new double[]{50,35,25 } },
            {DateTime.UtcNow.AddMinutes(-1), new double[]{50,40,25 } },
            {DateTime.UtcNow.AddMinutes(-0), new double[]{50,40,30 } },
        });
    }
}
