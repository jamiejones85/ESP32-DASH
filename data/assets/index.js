(function() {

    window.onload = function() {
        const websocket = getWebSocket("ws://"+ location.host + "/ws")
    }

    function onOpen(evt) {
        console.log("Socket Connected");
    }
       
    function onMessage(evt) {
        const json = JSON.parse(evt.data);
        json.forEach(element => {
            if (element.key == "soc") {
                updateBattery(element.value)
            } else if (element.key == "milliamps") {
                updatePower(parseInt(element.value / 1000))
            } else if (element.key == "battery-temp") {
                updateBatteryTemp((element.value/ 100).toFixed(1))
            } else if (element.key == "inverter-temp") {
                updateInverterTemp(element.value)
            } else if (element.key == "motor-temp") {
                updateMotorTemp(element.value)
            } else if (element.key == "speed") {
                updateSpeed(parseInt((element.value * 0.6213712)/100))
            } else if (element.key == "trip-ah") {
                updateTripAh(element.value.toFixed(2))
            } else if (element.key == "trip-km") {
                updateTripMiles((element.value * 0.6213712).toFixed(1))
            } else if (element.key == "average-ah-km") {
                updateAvgAh((element.value * 0.6213712).toFixed(1))
            }
        });

    }

    function onError(evt) {
        console.log("Socket Error")
    }

    function getWebSocket(wsUri) {
        const websocket = new WebSocket(wsUri);
           
        websocket.onopen = function(evt) {
           onOpen(evt)
        };
    
        websocket.onclose = function(evt) {
            console.log(evt)
        };
       
        websocket.onmessage = function(evt) {
           onMessage(evt)
        };
       
        websocket.onerror = function(evt) {
           onError(evt)
        };

        return websocket
     }

    function updateSpeed(speed) {
        updateValue("speed-value", speed)
    }

    function updateTripMiles(value) {
        updateValue("trip-miles-value", value)
    }

    function updateTripAh(value) {
        updateValue("trip-ah-value", value)
    }

    function updateAvgAh(value) {
        updateValue("avg-ah-value", value)
    }

    function updateBatteryTemp(temp) {
        updateValue("battery-temp", temp + "°C")
    }

    function updateMotorTemp(temp) {
        updateValue("motor-temp", temp + "°C")
    }

    function updateInverterTemp(temp) {
        updateValue("inverter-temp", temp + "°C")
    }


    function updateBattery(soc) {
        updateValue('percentage', soc + "%")
        const roundedValue = Math.ceil(soc / 10) * 10;
        for(i = 10; i <= 100; i = i + 10) {

            const el = document.getElementById('battery-' + i);
            if (roundedValue >= i) {
                el.classList.add('full-box')
            } else {
                el.classList.remove('full-box')
            }
        }
    }


    function updatePower(power) {
        updateValue('current', power + "a")
        const roundedValue = Math.ceil(power / 10) * 10;

        setRegen(roundedValue);
        setPower(roundedValue)
    }

    function setRegen(level) {
        const regenLevel = -level
        for(i = 10; i < 50; i = i + 10) {

            const el = document.getElementById('regen-' + i);
            if (regenLevel >= i) {
                el.classList.add('full-box')
            } else {
                el.classList.remove('full-box')
            }
        }
    }

    function setPower(level) {
        for(i = 10; i < 140; i = i + 10) {
            const el = document.getElementById('power-' + i);
            if (level >= i) {
                el.classList.add('full-box')
            } else {
                el.classList.remove('full-box')
            }
        }

    }


    function updateValue(selector, value) {
        const el = document.getElementById(selector)
        el.innerHTML = value
    }
})()