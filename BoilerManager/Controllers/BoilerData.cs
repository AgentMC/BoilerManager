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

        // POST api/<BoilerData>
        [HttpPost]
        public void Post([FromBody] string value)
        {
        }

        // PUT api/<BoilerData>/5
        [HttpPut("{id}")]
        public void Put(int id, [FromBody] string value)
        {
        }

        // DELETE api/<BoilerData>/5
        [HttpDelete("{id}")]
        public void Delete(int id)
        {
        }
    }
}
