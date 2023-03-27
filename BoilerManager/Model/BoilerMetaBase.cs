namespace BoilerManager.Model
{
    public abstract class BoilerMetaBase
    {
        public double BoilerTime { get; protected set; }
        public double BoilerWarm { get; protected set; }
        public DateTime LastNotified { get; protected set; }
    }
}