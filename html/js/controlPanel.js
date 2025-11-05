var tile = new Uint8Array(8);
var isPressingDown = false;

const controlPanelApp = new MacApp(123,
                                   "Ctrl Panel",
                                   false, //TODO temp set to false
                                   1, //0 = pointer, 1 = crosshair
                                   2, 19,
                                   122, 51,
                                   0 //0 = regular, 1 = rounded
                                  );

controlPanelApp.onMouseDownInWindow = async function (x, y) {
    isPressingDown = true;
    if (y >= 4 && y < 44) {
        if (x >= 11 && x < (11+40)){ // clicking on tile editor
            var tileX = Math.floor((x - 11) / 5);
            var tileY = Math.floor((y - 4) / 5);
            console.log("hitting at x: " + tileX + " y:" + tileY);
            setTilePixel(tileX, tileY, getTilePixel(tileX, tileY) === 0 ? 1 : 0);
            drawScreen();
            sleep(200);
            setAppBackground(controlPanelApp.id, controlPanelApp.buffer);
        }
        else if (x >= 62 && x < (62+48)) { // clicking on tile changer
            setBackgroundTile(tile);
            console.log("hitting on changer");
        } 
    }
}

controlPanelApp.onMouseUpInWindow = function (x, y) {
    isPressingDown = false;
}

document.addEventListener("mousemove", function(event) {
    if (isPressingDown)
    {
        
    }
});

/*document.onmousemove = function(e) 
{
    
    console.log("test");
}*/

document.addEventListener('onBgTileReceivedEvent', function(event)
{
    tile = event.detail.tile; 
    console.log("Received: " + JSON.stringify(tile));
    // set app background
    drawScreen();
    setAppBackground(controlPanelApp.id, controlPanelApp.buffer);
});

controlPanelApp.onInit = function () { // called when opened
    requestBackgroundTile();
}

function getTilePixel(x, y) {
    const byteReadIndex = y + Math.floor(x/8);
    const bitReadPosition = 7 - (x%8);
    var byte = tile[byteReadIndex];
    return ((byte >> bitReadPosition) & 1) === 1 ? 0: 1; // invert to match esp
}

function setTilePixel(x, y, color) {
    const byteIndex = y + Math.floor(x/8);
    const bitPosition = 7 - (x%8);
    
    if (color === 0) { //TODO temp inverted beccoz no time
        tile[byteIndex] |= 1 << bitPosition;
    }
    else {
        tile[byteIndex] &= ~(1 << bitPosition);
    }  
}

function drawScreen()
{
    controlPanelApp.clear();
        
    // draw big tile
    for (var y = 0; y < 8; y++) {
        for (var x = 0; x < 8; x++) {
            if (getTilePixel(x, y) == 1) {
                controlPanelApp.fillRect(12 + 5 * x, 5 + 5 * y, 4, 4, 1);
            }
        }
    }
    
    // draw preview
    for (var h = 0; h < 5; h++) {
        for (var w = 0; w < 6; w++) {
            controlPanelApp.drawBitmap(56+7 + (w * 8), 11 + (h * 8), tile, 8, 8);
        }
    }
    // remove excess
    controlPanelApp.fillRect(102+7, 11, 2, 40, 0);
    controlPanelApp.fillRect(55+7, 45, 48, 5, 0);

    // left side rect
    controlPanelApp.drawRect(4+7, 4, 42, 42, 1);
    
    // right side panel
    controlPanelApp.drawHLine(56+7, 4, 46, 1);
    controlPanelApp.drawHLine(56+7, 10, 46, 1); // header line
    controlPanelApp.drawHLine(56+7, 45, 46, 1);
    controlPanelApp.drawVLine(55+7, 5, 40, 1);
    controlPanelApp.drawVLine(102+7, 5, 40, 1);
}
