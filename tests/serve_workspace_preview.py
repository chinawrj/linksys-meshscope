"""Offline UI exercise server. Every write affects synthetic in-memory data only.

Run: python3 tests/serve_workspace_preview.py --port 8766 --scenario recovery
Scenarios: recovery, recovered, degraded, offline, nodes-only. Serves the actual compressed
ESP32 bundle under its Content Security Policy to catch embedded-only regressions.
Use --extra-nodes 16 for a larger, deeper graph during mobile zoom checks.
"""
import argparse
import copy
import sys
import time
from pathlib import Path
from http.server import ThreadingHTTPServer
from urllib.parse import urlparse

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import linksys_mesh_app as app
from tools.generate_esp32_meshscope_assets import build_assets, SOURCE_NAMES, WEB_ROOT, deterministic_gzip


class Preview(app.MeshRequestHandler):
    def end_headers(self):
        self.send_header('Content-Security-Policy', "default-src 'self'; img-src 'self' data:; style-src 'self'; script-src 'self'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'")
        super().end_headers()

    def serve_static(self, route):
        sources = {name:(WEB_ROOT/name).read_bytes() for name in SOURCE_NAMES}
        assets = {name:(mime,deterministic_gzip(content)) for name,mime,content in build_assets(sources)}
        name = 'index.html' if route in ('', '/') else route.lstrip('/')
        if name not in assets:
            self.send_error(404)
            return
        content_type, content = assets[name]
        self.send_response(200)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Encoding', 'gzip')
        self.send_header('Content-Length', str(len(content)))
        self.send_header('Cache-Control', 'no-cache')
        self.end_headers()
        self.wfile.write(content)

    def snapshot(self):
        data['meta']['updatedAt'] = app.now_iso()
        data['meta']['generation'] += 1
        data['meta']['topologyLock']['nextActionInSeconds'] = max(0, 60 - int(time.monotonic() - started))
        return copy.deepcopy(data)

    def do_GET(self):
        route = urlparse(self.path).path
        if route == '/api/status':
            return self.send_json({'connected':True, 'snapshotReady':True, 'managedConnection':True, 'router':'192.168.1.1', 'topologyLockRateLimitSeconds':data['meta']['topologyLock']['cooldownSeconds']})
        if route == '/api/topology':
            return self.send_json(self.snapshot())
        if route == '/api/mqtt-parent-steering':
            return self.send_json(steering)
        return super().do_GET()

    def do_POST(self):
        route = urlparse(self.path).path
        body = self.read_json()
        if route in ('/api/refresh', '/api/connect'):
            return self.send_json(self.snapshot())
        if route == '/api/device-configuration':
            data['meta']['topologyLock']['cooldownSeconds'] = body['topologyLockRateLimitSeconds']
            return self.send_json(body)
        if route == '/api/topology-lock':
            lock = data['meta']['topologyLock']
            lock['enabled'] = body['enabled']
            for mapping in body.get('nodes', []):
                row = next(row for row in lock['nodes'] if row['nodeId'] == mapping['nodeId'])
                parent = next(n for n in data['nodes'] if n['id'] == mapping['parentId'])
                row.update(expectedParentId=parent['id'], expectedParentName=parent['name'])
            return self.send_json(lock)
        if route == '/api/mqtt-parent-steering':
            steering['mode'] = body.get('mode', 'auto')
            steering['effectiveEnabled'] = steering['mode'] != 'force-off'
            return self.send_json(steering)
        if route == '/api/steer-node-parent':
            return self.send_json({'error':'Offline preview: request validated at UI only; no MQTT message sent.'}, 409)
        return self.send_json({'error':'Offline preview: no device action is performed.'}, 409)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8766)
    parser.add_argument('--scenario', choices=['recovery','recovered','degraded','offline','nodes-only'], default='recovery')
    parser.add_argument('--extra-nodes', type=int, choices=range(101), default=0, metavar='0..100')
    args = parser.parse_args()
    data = app.build_demo_topology('192.168.1.1')
    app.STATE.enable_demo()
    started = time.monotonic()
    lock = {'supported':True, 'enabled':True, 'state':'monitoring', 'cooldownSeconds':60, 'confirmationsRequired':3,
            'nextActionInSeconds':60, 'nodes':[], 'history':[], 'summary':{'total':5,'correct':4,'mismatch':1,'blocked':0,'offline':0}}
    for node in data['nodes']:
        node.update(phyRateMbps=2400 if node['id']=='demo-big-tree' else 1200, phyRateAgeSeconds=90)
        if node['isAuthority']:
            continue
        lock['nodes'].append({'nodeId':node['id'], 'status':'correct', 'expectedParentId':node['parentId'],
                             'expectedParentName':node['parentName'], 'currentParentId':node['parentId'], 'currentParentName':node['parentName']})
    lock['nodes'][-1].update(status='cooldown', expectedParentId='demo-yard-east', expectedParentName='Patio', actionInSeconds=60)
    data['meta'].update(edgeHosted=True, generation=1, topologyLock=lock)
    wired = data['nodes'][4]
    wired.update(isWired=True, connectionType='Ethernet', linkType='Ethernet', band='Ethernet', parentSource='topology-lock-wired-assignment')
    lock['nodes'][3]['status'] = 'wired-manual'
    spare = copy.deepcopy(data['nodes'][-1])
    spare.update(id='demo-offline', name='Storage room (offline)', online=False, clientCount=0)
    data['nodes'].append(spare)
    data['summary']['nodesTotal'] += 1
    for index in range(args.extra_nodes):
        parent = data['nodes'][0] if index % 4 == 0 else data['nodes'][-1]
        extra = copy.deepcopy(data['nodes'][1])
        extra.update(id=f'demo-extra-{index}', name=f'Extra room {index + 1:02}',
                     parentId=parent['id'], parentName=parent['name'], clientCount=0,
                     ipAddress=f'192.0.2.{index + 1}', online=True)
        data['nodes'].append(extra)
        lock['nodes'].append({'nodeId':extra['id'], 'status':'correct',
                             'expectedParentId':parent['id'], 'expectedParentName':parent['name'],
                             'currentParentId':parent['id'], 'currentParentName':parent['name']})
        data['summary']['nodesTotal'] += 1
        data['summary']['nodesOnline'] += 1
    steering = {'mode':'auto','state':'available','supported':True,'available':True,'roundTrip':True,'effectiveEnabled':True,
                'reason':'Synthetic broker round trip for UI review. No router connection.', 'testedAt':app.now_iso(),
                'nodeHealth':[{'childId':'demo-road-south','childName':'Garage','targetParentId':'demo-yard-east','targetParentName':'Patio',
                               'state':'watching','band':'5GH','consecutiveFailures':1,'failureThreshold':2,'totalFailures':3,
                               'targetParentOnline':True,'targetParentOnlineChildren':3,'lastTargetBssid':'02:00:00:00:11:22',
                               'lastTargetChannel':149,'lastRequestPublished':True,'lastCommandEchoed':True,
                               'reason':'The requested parent was not observed in two fresh snapshots.'}]}
    if args.scenario == 'recovered':
        child = next(node for node in data['nodes'] if node['id'] == 'demo-road-south')
        row = next(item for item in lock['nodes'] if item['nodeId'] == child['id'])
        row.update(status='correct', expectedParentId=child['parentId'], expectedParentName=child['parentName'])
        health = steering['nodeHealth'][0]
        health.update(state='recovered', consecutiveFailures=0, totalFailures=13, successfulMoves=4,
                      targetParentId=child['parentId'], targetParentName=child['parentName'],
                      lastOperationId=4, lastFailureAt='2026-09-06T01:45:07Z',
                      lastSuccessAt='2026-09-05T03:46:19Z', lastRecoveredAt='2026-09-06T13:00:00Z',
                      reason='Requested Parent recovered in two consecutive fresh topology generations')
        steering['operation'] = {'id':4,'childId':child['id'],'parentId':child['parentId'],
                                 'parentName':child['parentName'],'state':'failed',
                                 'requestedAt':'2026-09-06T01:42:00Z',
                                 'error':'Historical verification timeout; Parent later recovered'}
        lock['summary'].update(correct=5, mismatch=0)
    elif args.scenario == 'degraded':
        data['meta'].update(topologyDegraded=True, routerConnected=False)
        lock['summary'].update(unknown=5, correct=0, mismatch=0)
        for node in data['nodes']:
            if not node['isAuthority']:
                node['parentSource'] = 'topology-lock-degraded-assignment'
        for row in lock['nodes']:
            row['status'] = 'data-unavailable'
    elif args.scenario == 'offline':
        for node in data['nodes']:
            node['online'] = False
        data['summary']['nodesOnline'] = 0
    elif args.scenario == 'nodes-only':
        data['meta']['clientDetails'] = 'nodes-only'
        data['clients'] = []
        data['summary'].update(clientsOnline=0, clientsKnown=0)
    print(f'Offline {args.scenario} preview: http://127.0.0.1:{args.port}', flush=True)
    ThreadingHTTPServer(('127.0.0.1', args.port), Preview).serve_forever()
