function generateBoxShadow(maxHeight, step, threshold) {
    var r = '';
    var thPx = threshold * maxHeight;
    for (var i = 0; i <= maxHeight; i += step) {
        if (i !== 0) {
            r += ', ';
        }
        r += 0 + 'px ' + i + 'px ' + (i + step > thPx && i <= thPx ? 3 : 0) + 'px ' + (i <= thPx ? '#ff0000' : '#115599') + Math.max(Math.min(192, 12 * step), 16).toString(16);
    }
    return r;
}
function drawCylinder(heightCylinder, stepPx, cylinderWarm) {
    var x = generateBoxShadow(heightCylinder, stepPx, cylinderWarm);
    $('#top')[0].style.boxShadow = x;
    $('#bottom')[0].style.marginTop = heightCylinder - 100 + 'px';
}
function setMetrics(cylinderWarm, cylinderTime) {
    $('#boilerPercent')[0].innerText = (cylinderWarm * 100).toFixed(2);
    $('#boilerTime')[0].innerText = (cylinderTime).toFixed(0);
}
function dateToLocal(date) {
    var d = date ? new Date(date) : new Date();
    return d.toLocaleString();
}
var globalChart;
function update(gHeightPx, gStepPx) {
    fetch("/api/BoilerData")
        .then(response => {
            var statusPromise;
            if (response.ok) {
                statusPromise = response.json()
                    .then(json => {
                        var cylinderWarm = json.boilerWarm;
                        var cylinderTime = json.boilerTime;
                        drawCylinder(gHeightPx, gStepPx, cylinderWarm);
                        setMetrics(cylinderWarm, cylinderTime);
                        //---------------
                        var data = {
                            labels: [],
                            datasets: [{
                                label: 'Top reading',
                                backgroundColor: 'rgb(255, 99, 132)',
                                borderColor: 'rgb(255, 99, 132)',
                                data: [],
                            }, {
                                label: 'Mid reading',
                                backgroundColor: 'rgb(99, 255, 132)',
                                borderColor: 'rgb(132, 255, 99)',
                                data: [],
                            }, {
                                label: 'Bottom reading',
                                backgroundColor: 'rgb(132, 99, 255)',
                                borderColor: 'rgb(99, 255, 132)',
                                data: [],
                            }]
                        };
                        for (var k of Object.keys(json.readings)) {
                            data.labels.push(dateToLocal(k));
                            values = json.readings[k];
                            for (var i = 0; i < 3; i++) {
                                data.datasets[i].data.push(values[i]);
                            }
                        }
                        if (globalChart) globalChart.destroy();
                        globalChart = new Chart($('#myChart'), { type: 'line', data: data, options: { animation:false } });

                        //---------------
                        setTimeout(() => update(gHeightPx, gStepPx), 1000);
                        //---------------
                        return Promise.resolve(`Обновлено: клиент: ${dateToLocal()}, сервер: ${dateToLocal(json.lastNotified)}`);
                    });
            } else {
                statusPromise = response.text()
                    .then(text => {
                        return Promise.resolve(`Обновлено: клиент: ${dateToLocal()}, ошибка HTTP ${response.status} ${response.statusText}: [${text}]`);
                    });
            }
            return statusPromise;
        })
        .then(s => $('#loader')[0].innerText = s);
}
