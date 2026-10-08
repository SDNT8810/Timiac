"""Provision password authentication on Firebase's free plan; never print secrets."""
import argparse
import getpass
import json
import os
from pathlib import Path
import subprocess
import time
import urllib.error
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', default='timiac-control-20261008')
    parser.add_argument('--username', default='Andrea')
    parser.add_argument('--email', default='andrea@timiac.it')
    parser.add_argument('--uid', default='andrea')
    args = parser.parse_args()
    token = subprocess.run(['gcloud.cmd' if os.name == 'nt' else 'gcloud', 'auth', 'print-access-token'],
                           capture_output=True, text=True, check=True).stdout.strip()
    headers = {'Authorization': 'Bearer ' + token, 'X-Goog-User-Project': args.project,
               'Content-Type': 'application/json'}

    def call(url, method='GET', body=None):
        request = urllib.request.Request(url, data=json.dumps(body).encode() if body is not None else None,
                                         headers=headers, method=method)
        try:
            with urllib.request.urlopen(request, timeout=45) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            data = json.load(error)
            raise RuntimeError(str(error.code) + ': ' + data.get('error', {}).get('message', 'API request failed')) from None

    base = 'https://firebase.googleapis.com/v1beta1/'
    apps = call(base + 'projects/' + args.project + '/webApps').get('apps', [])
    if not apps:
        operation = call(base + 'projects/' + args.project + '/webApps', 'POST', {'displayName': 'Timiac Control'})
        for _ in range(30):
            operation = call(base + operation['name'])
            if operation.get('done'):
                if operation.get('error'):
                    raise RuntimeError(operation['error'].get('message', 'Could not create the Firebase app'))
                break
            time.sleep(2)
        apps = call(base + 'projects/' + args.project + '/webApps').get('apps', [])
    if not apps:
        raise RuntimeError('Firebase app creation is still pending; run again shortly')
    config = call(base + apps[0]['name'] + '/config')
    local = Path(__file__).resolve().parents[1] / '.local'
    local.mkdir(exist_ok=True)
    (local / 'firebase-config.json').write_text(json.dumps(config, indent=2) + '\n', encoding='utf-8')
    auth_base = 'https://identitytoolkit.googleapis.com/'
    config_url = auth_base + 'admin/v2/projects/' + args.project + '/config'
    try:
        call(config_url)
    except RuntimeError as error:
        if 'CONFIGURATION_NOT_FOUND' not in str(error) and not str(error).startswith('404:'):
            raise
        raise RuntimeError('Open https://console.firebase.google.com/project/' + args.project +
                           '/authentication and click Get started on the Spark plan, then run this script again') from None
    call(config_url + '?updateMask=signIn.email,authorizedDomains', 'PATCH', {
        'signIn': {'email': {'enabled': True, 'passwordRequired': True}},
        'authorizedDomains': ['localhost', 'control.timiac.it', args.project + '.firebaseapp.com'],
    })
    password = os.environ.get('TIMIAC_ADMIN_PASSWORD') or getpass.getpass('Dashboard password: ')
    if len(password) < 8:
        raise RuntimeError('Use a password with at least eight characters')
    accounts = auth_base + 'v1/projects/' + args.project + '/accounts'
    found = call(accounts + ':lookup', 'POST', {'localId': [args.uid]}).get('users', [])
    values = {'localId': args.uid, 'email': args.email, 'password': password, 'displayName': args.username}
    if found:
        call(accounts + ':update', 'POST', values)
    else:
        call(accounts, 'POST', values)
    print('Password account provisioned: ' + args.username)
    print('Firebase web configuration saved in .local/firebase-config.json; no billing account linked.')


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print('Provisioning failed: ' + str(error))
        raise SystemExit(1) from None
