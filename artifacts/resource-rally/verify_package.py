"""Verify the resource rally playtest package against its source and local checks."""
from pathlib import Path
import datetime
import hashlib
import json
import re
import subprocess
import os
import zipfile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'artifacts/resource-rally'
STORAGE = Path('/Volumes/Codex Storage/Cinderline/Android')
APK = STORAGE / 'packages/Cinderline-Android-arm64.apk'
BUILD_TOOLS = STORAGE / 'sdk/build-tools/35.0.1'

def sha(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(chunk)
    return result.hexdigest()

def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))

def run(tool, args, filename):
    env = dict(os.environ, JAVA_HOME='/opt/homebrew/opt/openjdk@21')
    result = subprocess.run([str(BUILD_TOOLS / tool), *args], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
    (OUT / filename).write_text(result.stdout)
    result.check_returncode()
    return result.stdout

assert 'AutomationTool exiting with ExitCode=0 (Success)' in (OUT/'package-android.log').read_text()
source = read(OUT/'source-sha256.json')
for path, digest in source.items():
    assert sha(ROOT/path) == sha(STORAGE/'workspace'/path) == digest, path
assert sha(ROOT/'Source/Cinderline/Private/Sim/SimulationProduction.cpp') == read(OUT/'before-sha256.json')['Source/Cinderline/Private/Sim/SimulationProduction.cpp']
report_path = ROOT/'Saved/Automation/Integration/20260915T104637Z-19855/index.json'
report = read(report_path)
assert report['succeeded'] == 41 and report['succeededWithWarnings'] == report['failed'] == report['notRun'] == 0
assert '100% tests passed, 0 tests failed out of 4' in (OUT/'tests-portable.log').read_text()
preview = read(OUT/'preview-verification.json')
assert preview['complete'] and preview['protected_unchanged'] and preview['visual_review'] == 'passed'
assert sha(ROOT/preview['module']) == preview['module_sha256']
for item in preview['cases']:
    assert sha(ROOT/item['screenshot']) == item['sha256']
previous = read(OUT/'previous-android-package.json')
assert sha(previous['backup']) == previous['sha256'] and sha(APK) != previous['sha256']
badging = run('aapt', ['dump', 'badging', str(APK)], 'apk-badging.txt')
name, code, version = re.search(r"package: name='([^']+)' versionCode='([^']+)' versionName='([^']+)'", badging).groups()
assert (name, code, version) == ('com.cinderline.game', '3', '0.1.2')
signature = run('apksigner', ['verify', '--verbose', '--print-certs', str(APK)], 'apk-signature.txt')
old_signature = run('apksigner', ['verify', '--verbose', '--print-certs', previous['backup']], 'previous-apk-signature.txt')
cert = lambda text: re.search(r'certificate SHA-256 digest: (\w+)', text).group(1)
assert cert(signature) == cert(old_signature)
run('zipalign', ['-c', '-P', '16', '-v', '4', str(APK)], 'apk-alignment.txt')
with zipfile.ZipFile(APK) as archive:
    entries = archive.namelist()
    libraries = [p for p in entries if p.startswith('lib/arm64-v8a/') and p.endswith('.so')]
    data = [p for p in entries if p.startswith('assets/') and p.endswith(('.pak', '.utoc', '.ucas', '.obb.png'))]
    assert 'AndroidManifest.xml' in entries and 'lib/arm64-v8a/libUnreal.so' in libraries and data
    unreal_sha = hashlib.sha256(archive.read('lib/arm64-v8a/libUnreal.so')).hexdigest()
receipt = {
    'schema': 'cinderline.resource_rally.v1',
    'created_at_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
    'source_sha256': source, 'packaged_workspace_source_matches': True,
    'simulation_production_unchanged': True, 'save_version': 13, 'protocol_version': 10,
    'unreal': {'report': str(report_path), 'report_sha256': sha(report_path), 'passed': 41,
               'failed': 0, 'warnings': 0, 'module_sha256': preview['module_sha256']},
    'portable': {'passed_suites': 4, 'report': str(OUT/'tests-portable.log'),
                 'report_sha256': sha(OUT/'tests-portable.log'),
                 'production_exit_binary_sha256': sha(ROOT/'Saved/P1FormationBuild/CinderlineProductionExitTests')},
    'visual_review': {'receipt': str(OUT/'preview-verification.json'), 'sha256': sha(OUT/'preview-verification.json'),
                      'passed': True, 'cases': 2, 'native_touch_proof': False},
    'apk': {'path': str(APK), 'sha256': sha(APK), 'size_bytes': APK.stat().st_size,
            'package': name, 'version_code': int(code), 'version_name': version,
            'signature_verified': True, 'development_signed': True,
            'same_signer_as_previous': True, 'signer_certificate_sha256': cert(signature),
            'zipalign_16kb_verified': True, 'unreal_library_sha256': unreal_sha,
            'cooked_data_entries': data},
    'physical_android_retest': False, 'installed_on_device': False,
    'ios_package_updated': False, 'public_backend_deployed': False, 'p2_started': False,
    'limitations': ['Mac controller and simulation checks are not native device touch proof.',
                    'Scripted previews show producer controls and Anchor Jobs; the new action-bar Rally is not separately captured.',
                    'A depleted or unreachable selected mine retains the ground-rally fallback; Auto Mine restores automatic allocation.']
}
(OUT/'verification.json').write_text(json.dumps(receipt, indent=2)+'\n')
print(json.dumps({'apk': receipt['apk'], 'checks_passed': True}, indent=2))
