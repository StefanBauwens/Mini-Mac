document.addEventListener('websocketOpenedEvent', async function(event) {
    registerApp(controlPanelApp);
    //sleep(200); // wait before registering next
    //registerApp(hello);
    sleep(200); // wait before registering next
    registerApp(puzzle);
});
