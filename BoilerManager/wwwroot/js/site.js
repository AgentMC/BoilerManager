const MAX_UPDATE_INTERVAL = 80;
const NOTIFICATION_THRESHOLD = 0.05;
const state = {
    OK: "#ffffff",
    WARNING: "#ffff00",
    ERROR: "#ff0000"
}

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
function setMetrics(json) {
    var formatted = {
        percent: (json.boilerWarm * 100).toFixed(2),
        time: (json.boilerTime).toFixed(0)
    };
    $('#boilerPercent')[0].innerText = formatted.percent;
    $('#boilerTime')[0].innerText = formatted.time;
    return formatted;
}
function dateToLocal(date) {
    var d = date ? new Date(date) : new Date();
    return d.toLocaleString();
}
function getChartAr() {
    var w = document.body.clientWidth;
    if (w > 450) return 2;
    if (w > 380) return 1.5;
    if (w > 280) return 1;
    return 0.75;
}
function notificationsEnabled() { return Notification.permission == 'granted'; }
function closeAndReset(percent) {
    if (document.currentNotification) document.currentNotification.close();
    document.currentTopPercent = percent;
}
var globalChart;
function update(gHeightPx, gStepPx, scopeLength) {
    document.currentTimeout = -1;
    fetch(`./api/BoilerData?length=${scopeLength}`)
        .then(response => {
            var statusPromise;
            if (response.ok) {
                statusPromise = response.json()
                    .then(json => {
                        //--------------- Draw boiler
                        drawCylinder(gHeightPx, gStepPx, json.boilerWarm);
                        var formattedMetrics = setMetrics(json);
                        //--------------- Draw chart
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
                                borderColor: 'rgb(99, 255, 132)',
                                data: [],
                            }, {
                                label: 'Bottom reading',
                                backgroundColor: 'rgb(132, 99, 255)',
                                borderColor: 'rgb(132, 99, 255)',
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
                        globalChart = new Chart($('#myChart'), { type: 'line', data: data, options: { animation: false, aspectRatio: getChartAr() } });
                        //--------------- Schedule update
                        document.currentTimeout = setTimeout(() => update(gHeightPx, gStepPx, document.scopeLength), 10000);
                        //--------------- Notify if necessary and enabled
                        if (notificationsEnabled()) {
                            if (!json.isHeating) {
                                //if changed negatively - close and reset top
                                closeAndReset(json.boilerWarm);
                            } else if (json.boilerWarm - (document.currentTopPercent ?? 0) >= NOTIFICATION_THRESHOLD) {
                                //if changed positively over threshold - close, set top and notify anew
                                closeAndReset(json.boilerWarm);
                                document.currentNotification = new Notification(`Boiler is heating! Now at ${formattedMetrics.percent}%, ${formattedMetrics.time} min available.`, { tag: 'BM01' });
                            }
                        }
                        //--------------- Indicate possible battery down
                        var dateDiff = (new Date() - Date.parse(json.lastNotified)) / 1000;
                        return Promise.resolve({ Message: `server: ${dateToLocal(json.lastNotified)}`, State: (dateDiff > MAX_UPDATE_INTERVAL ? state.WARNING : state.OK) });
                    });
            } else {
                statusPromise = response.text()
                    .then(text => {
                        return Promise.resolve({ Message: `HTTP error ${response.status} ${response.statusText}: [${text}]`, State: state.ERROR });
                    });
            }
            return statusPromise;
        })
        .then(s => {
            var ldr = $('#loader')[0];
            ldr.innerText = `Refreshed: client: ${dateToLocal()}, ${s.Message}.`;
            ldr.style.backgroundColor = s.State;
            var visual = $('#visuals')[0];
            if (s.State == state.OK) {
                ldr.style.fontSize = 'inherit';
                ldr.style.fontWeight = 'inherit';
                ldr.style.border = 'none';
                visual.style.opacity = 'inherit';
            } else {
                ldr.style.fontSize = 'x-large';
                ldr.style.fontWeight = 'bold';
                ldr.style.border = '1px solid red';
                visual.style.opacity = '50%';
            }
        });
}