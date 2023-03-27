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
        public BoilerMetaResponse Get([FromQuery] int length = -1)
        {
            return BoilerMeta.Default.ToResponse(length);
        }

        private readonly string[] DeviceIDs = new[] { "28DC331A0E0000BA", "2872BB1A0E000000", "287E281A0E000084" };

        // POST api/<BoilerData>
        [HttpPost]
        public void Post([FromBody] Dictionary<string, double> value)
        {
            const int ExpectedCount = 3;
            if (value.Count != ExpectedCount) return;
            var doubles = new double[ExpectedCount];
            for (int i = 0; i < ExpectedCount; i++)
            {
                double? d = value.TryGetValue(DeviceIDs[i], out double x) ? x : null;
                if (d == null) return;
                doubles[i] = d.Value;
            }
            BoilerMeta.Default.Add(doubles);
        }
    }
}
