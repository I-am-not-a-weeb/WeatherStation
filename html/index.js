function updateWiFi()
{
    const xhttp = new XMLHttpRequest();
    xhttp.onreadystatechange = function() {
        if (this.readyState == 4 && this.status == 200)
        {
            document.getElementById("WIFI").innerHTML = this.responseText;
        }
    }
    document.getElementById("WIFI").innerHTML = "Updating...";
    xhttp.open("GET", "updateWifi", true);
    xhttp.send()
}

