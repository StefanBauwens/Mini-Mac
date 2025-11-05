const websocketOpenedEvent = new CustomEvent('websocketOpenedEvent');
var gateway = `ws://192.168.0.123/ws`; //TODO replace with the IP given on your display

class MacApp {
    constructor(id, title, isDesktopApp, cursorType, windowX, windowY, windowWidth, windowHeight, windowType) {
        this.id = id;
        this.title = title;
        this.isDesktopApp = isDesktopApp;
        this.cursorType = cursorType;
        this.windowX = windowX;
        this.windowY = windowY;
        this.windowWidth = windowWidth;
        this.windowHeight = windowHeight;
        this.windowType = windowType;
        
        this.buffer = new Uint8Array(Math.ceil(windowWidth * windowHeight / 8.0));
        //this.buffer.fill(240); //TODO temp
        
        // Callbacks
        this.onMouseDownInWindow = function (x, y) {}; 
        this.onMouseUpInWindow = function (x, y) {};
        this.onInit = function() {};
    }    
    
    /* draw functions */
    clear() {
        this.buffer.fill(0);
    }
    
    drawPixel(x, y, color){ // color 1 or 0
        if (x < 0 || x >= this.windowWidth || y < 0 || y >= this.windowHeight) {
            return;
        }
        var byteIndex = (y * Math.floor(this.windowWidth / 8)) + Math.floor(x / 8);
        //console.log("byteindex: " + byteIndex + " x: " + x +" y: " + y + " windowwidth: " + this.windowWidth);
        var bitPosition = 7 - (x % 8);
        if (color === 1) {
            this.buffer[byteIndex] |= 1 << bitPosition;
        }
        else {
            this.buffer[byteIndex] &= ~(1 << bitPosition);
        }
    }
    
    drawHLine(x0, y0, length, color){
        for (var i = 0; i < length; i++){
            this.drawPixel(x0 + i, y0, color);
        }
    }
    
    drawVLine(x0, y0, length, color){
        for (var i = 0; i < length; i++){
            this.drawPixel(x0, y0 + i, color);
        }
    }
    
    drawRect(x0, y0, width, height, color)
    {
        this.drawHLine(x0, y0, width, color);
        this.drawHLine(x0, y0 + height - 1, width, color);
        this.drawVLine(x0, y0, height, color);
        this.drawVLine(x0 + width - 1, y0, height, color);
    }
    
    fillRect(x0, y0, width, height, color){
        for (var w = 0; w < width; w++) {
            for (var h = 0; h < height; h++) {
                this.drawPixel(x0 + w, y0 + h, color);
            }
        }
    }
    
    drawBitmap(x0, y0, bitmapBuffer, width, height){ 
        for (var y = 0; y < height; y++) {
            for (var x = 0; x < width; x++) {
                const byteReadIndex = (y * Math.floor(width / 8)) + Math.floor(x/8);
                const bitReadPosition = 7 - (x%8);
                
                this.drawPixel(x + x0, y + y0, ((bitmapBuffer[byteReadIndex] >> bitReadPosition) & 1) === 1?0:1); //TODO temp invert to fix esp   
            }
        }
    }
}

//
var appsDictionary = {};

function requestBackgroundTile() {
    websocket.send(JSON.stringify({bgTileRequest:true}));
}

function setBackgroundTile(tile) { // tile is uint8array of length 8
    var bgTile = {
      bg: tile  
    };
    var json = JSON.stringify(bgTile);
    console.log("Sending tiledata: " + json);
    websocket.send(json);
}

function registerApp(app) {
    appsDictionary[app.id] = app;
    
    var subObj = {
        app: app.id,
        title: app.title,
        isDesktopApp: app.isDesktopApp,
        cursorType: app.cursorType,
        windowX: app.windowX,
        windowY: app.windowY,
        windowWidth: app.windowWidth,
        windowHeight: app.windowHeight,
        windowType: app.windowType
    };
    
    var json = JSON.stringify(subObj);
    console.log("Register app: " + json);
    websocket.send(json); //TODO wait till websocket loaded
}

function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

var updateMouse = true;

async function setAppBackground(id, buffer) {
    var delayT = 5; //100
    updateMouse = false;
    var startJson = "{\"start\":" + id + "}";
    websocket.send(startJson); // send json with id 
    await sleep(delayT);

    // send data as hex
    var fullStr = "";
    for(var i = 0; i < buffer.length; i++)
    {
        fullStr += buffer[i].toString(16).padStart(2, '0').toUpperCase();
        if (i%200 == 0 && i !== 0) // max length = 250
        {
            console.log("sending part: " + fullStr + " length: " + fullStr.length);
            await sleep(delayT); 
            websocket.send(fullStr);
            fullStr = "";
        }
    }
    console.log("last part fullstr: " + fullStr + "length: " + fullStr.length);
    websocket.send(fullStr);
    websocket.send("END");
    updateMouse = true;
}

var websocket;
// Init web socket when the page loads
window.addEventListener('load', onload);

var mouseDown = 0;

var timeSinceSent = Date.now();

document.onmousemove = function(e) {
    if (!updateMouse)
    {
        return;
    }
    var event = e || window.event;
    var mouseX = event.clientX;
    var mouseY = event.clientY;
    //console.log("mousex: " + mouseX + " mouseY: " + mouseY);
    var mappedX = Math.round(((mouseX) / window.innerWidth) * 128);
    var mappedY = Math.round(((mouseY) / window.innerHeight) * 128);
    
    var millis = Date.now() - timeSinceSent;
    if(millis > 25)
    {
        var json = "{\"mouseX\":" + mappedX + ",\"mouseY\":" + mappedY + ",\"mouseDown\":" + mouseDown + "}";
        //console.log("json: " + json);
        websocket.send(json);
        timeSinceSent = Date.now();
    }
}

document.onmousedown = function(evt) { 
    if (!updateMouse)
    {
        return;
    }
    if (evt.button == 0)
    {
        mouseDown = 1;
        var json = "{\"mouseDown\":" + mouseDown + "}";
        console.log("json: " + json);
        websocket.send(json);
    }
}

document.onmouseup = function(evt) {
    if (!updateMouse)
    {
        return;
    }
    if (evt.button == 0)
    {
        mouseDown = 0;
        var json = "{\"mouseDown\":" + mouseDown + "}";
        console.log("json: " + json);
        websocket.send(json);
    }
}

function onload(event) {
    initWebSocket();
}

function initWebSocket() {
    console.log('Trying to open a WebSocket connection…');
    websocket = new WebSocket(gateway);
    websocket.onopen = onOpen;
    websocket.onclose = onClose;
    websocket.onmessage = onMessage;
}

function onOpen(event) {
    console.log('Connection opened');
    document.dispatchEvent(websocketOpenedEvent);
    //getReadings();
}

function onClose(event) {
    console.log('Connection closed');
    setTimeout(initWebSocket, 2000);
}

// Function that receives the message from the ESP
function onMessage(event) {
    console.log("RECEIVED:" + event.data);
    var receivedObj = JSON.parse(event.data);
    if ("request" in receivedObj) // request app buffer send (happens on opening the app)
    {
        console.log("Requesting: " + receivedObj.request);  
        var requestId = receivedObj.request;
        if (requestId in appsDictionary)
        {
            appsDictionary[requestId].onInit(); //should set app background
            //setAppBackground(requestId, appsDictionary[requestId].buffer);
        }
    }
    else if ("mouseDown" in receivedObj) // send on click with relative points to app
    {
        var appId = receivedObj.app;
        if (appId in appsDictionary)
        {
            if (receivedObj.mouseDown == 1)
            {
                appsDictionary[appId].onMouseDownInWindow(receivedObj.x, receivedObj.y);
            }
            else
            {
                appsDictionary[appId].onMouseUpInWindow(receivedObj.x, receivedObj.y);
            }
        }
    }
    else if ("bgTileResponse" in receivedObj)
    {
        const values = Object.values(receivedObj.bgTileResponse);

        // Convert values to Uint8Array
        const tile = new Uint8Array(values)
        
        const onBgTileReceivedEvent = new CustomEvent('onBgTileReceivedEvent', { detail: { tile: tile } });

        document.dispatchEvent(onBgTileReceivedEvent)
    }
}