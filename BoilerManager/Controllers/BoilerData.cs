using BoilerManager.Model;
using Microsoft.AspNetCore.Mvc;

// For more information on enabling Web API for empty projects, visit https://go.microsoft.com/fwlink/?LinkID=397860

namespace BoilerManager.Controllers
{
    [Route("api/[controller]")]
    [ApiController]
    public class BoilerData : ControllerBase
    {
        // GET: api/<BoilerData>
        [HttpGet, ResponseCache(Duration = 10)]
        public BoilerMeta Get()
        {
            return BoilerMeta.Default;
        }

        // GET api/<BoilerData>/5
        [HttpGet("{id}")]
        public string Get(int id)
        {
            return "value";
        }

        private readonly string[] DeviceIDs = new[] { "ID1", "ID2", "ID3" };

        // POST api/<BoilerData>
        [HttpPost]
        public void Post([FromBody] Dictionary<string, double> value)
        {
            if (value.Count != 3) return;
            var doubles = new double[value.Count];
            for (int i = 0; i < value.Count; i++)
            {
                double? d = value.TryGetValue(DeviceIDs[i], out double x) ? x : null;
                if (d == null) return;
                doubles[i] = d.Value;
            }
            BoilerMeta.Default.Add(doubles);
        }
    }
}
