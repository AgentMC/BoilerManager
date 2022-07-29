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






        //----------------------------





        private static readonly Random _rnd = new();
        private static readonly Thread _pureChaos = GetThread();

        private static Thread GetThread()
        {
            foreach (var x in new Dictionary<DateTime, double[]>()
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
            })
            {
                Default.Readings.Add(x.Key, x.Value);
            }

            var timestamp = Default.Readings.Keys.Max();
            var lastEntry = Default.Readings[timestamp];
            Default.UpdateStats(timestamp, lastEntry);

            var t = new Thread(delegate ()
            {
                var lastValues = Default.Readings[Default.Readings.Keys.Max()];
                var upDown = 2;
                while (true)
                {
                    Thread.Sleep(3000);

                    var newValues = new double[] { lastValues[0] + _rnd.NextDouble() * 3 - upDown, lastValues[1] + _rnd.NextDouble() * 3 - upDown, lastValues[2] + _rnd.NextDouble() * 3 - upDown };
                    Default.Add(newValues);
                    
                    var total = newValues.Sum();
                    if((total < 60 && upDown >0) ||(total > 165 && upDown <0))
                    {
                        upDown *= -1;
                    }
                    lastValues = newValues;
                }
            })
            { IsBackground = true };
            t.Start();
            return t;
        }
    }
}
