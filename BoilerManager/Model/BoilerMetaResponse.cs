namespace BoilerManager.Model
{
    public class BoilerMetaResponse : BoilerMetaBase
    {
        public BoilerMetaResponse(DateTime timestamp, double boilerWarm, double boilerTime)
        {
            LastNotified = timestamp;
            BoilerWarm = boilerWarm;
            BoilerTime = boilerTime;
        }

        public Dictionary<DateTime, double[]> Readings { get; init; } = new();
        public bool IsHeating { get; set; }
    }
}
