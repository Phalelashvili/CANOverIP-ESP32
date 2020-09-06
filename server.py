from flask import Flask, escape, request

app = Flask(__name__)

devices = ["0.0.0.0", "0.0.0.0"]

def getOtherDevicesIP(ip):
    index = devices.index(ip)
    if index == 0:
        return devices[1]
    return devices[0]

@app.route('/update')
def update():
    publicIP = request.environ.get('HTTP_X_REAL_IP', request.remote_addr)
    localIP = request.args.get("localIP", "0.0.0.0")
    id = int(request.args['id']) # it's required
    devices[id] = localIP # localip for now

    return f'{escape(getOtherDevicesIP(localIP))}' # localip for now