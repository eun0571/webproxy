function FindProxyForURL(url, host) {
    if (shExpMatch(url, "http://13.125.98.84:9000/*")) {
        return "PROXY 13.125.98.84:8000";
    }
    return "DIRECT";
}