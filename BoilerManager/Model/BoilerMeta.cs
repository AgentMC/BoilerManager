namespace BoilerManager.Model
{
    public class BoilerMeta
    {
        const double FullWarm = 45.0, DefCold = 25;

        public double BoilerWarm { get; private set; }
        public double BoilerTime { get; private set; }
        public DateTime LastNotified { get; private set; }

        public Dictionary<DateTime, double[]> Readings { get; init; }

        public BoilerMeta(Dictionary<DateTime, double[]> readings)
        {
            Readings = readings;
            Update();
        }

        public void Update()
        {
            LastNotified = Readings.Keys.Max();

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

        private static readonly Random _rnd = new();
        private static readonly Thread _pureChaos = GetThread();

        private static Thread GetThread()
        {
            var t = new Thread(delegate ()
            {
                while (true)
                {
                    Thread.Sleep(3000);
                    var lastValues = Default.Readings[Default.Readings.Keys.Max()];
                    Default.Readings.Add(DateTime.UtcNow, new double[] { lastValues[0] + _rnd.NextDouble() * 3 - 2, lastValues[1] + _rnd.NextDouble() * 3 - 2, lastValues[2] + _rnd.NextDouble() * 3 - 2 });
                    Default.Update();
                }
            })
            { IsBackground = true };
            t.Start();
            return t;
        }
    }
}
