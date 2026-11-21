"""Build and verify a real CTest selection, with optional fresh-state comparison."""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
import uuid


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def execution_result(output, exit_code, expected):
    statuses = {}
    for name, status in re.findall(r'Test\s+#\d+:\s+(\S+)\s+\.+\s*([^\r\n]+)', output):
        statuses[name] = status.strip()
    executed = {name for name, status in statuses.items()
                if status.startswith(('Passed', '***Failed', '***Exception', '***Timeout'))}
    summary = re.search(r'\d+% tests passed, (\d+) tests failed out of (\d+)', output)
    passed = (exit_code == 0 and summary is not None and int(summary[1]) == 0
              and int(summary[2]) == len(expected) and executed == set(expected)
              and all(statuses[name].startswith('Passed') for name in expected))
    return {'gatePassed': passed, 'summary': summary[0] if summary else None,
            'executedTests': sorted(executed), 'statuses': statuses}


def state_candidates(baseline, source_root, build):
    if not isinstance(baseline, dict) or any(not isinstance(name, str) or not name
            or not isinstance(checksum, str) or not re.fullmatch(r'[0-9a-fA-F]{64}', checksum)
            for name, checksum in baseline.items()):
        raise ValueError('State baseline must map nonempty paths to SHA-256 strings.')
    expected = {}
    for name, checksum in baseline.items():
        checksum = checksum.lower()
        relative = Path(name)
        original = relative if relative.is_absolute() else source_root / relative
        expected[original] = checksum
        if relative.parts[0] == 'build':
            expected[build / Path(*relative.parts[1:])] = checksum
    return expected


def state_times(expected):
    return {path: path.stat().st_mtime_ns if path.is_file() else None for path in expected}


def compare_states(expected, before):
    fresh = {str(path): digest(path) for path, old_time in before.items()
             if path.is_file() and path.stat().st_mtime_ns != old_time}
    different = {name: {'baseline': expected[Path(name)], 'current': checksum}
                 for name, checksum in fresh.items() if checksum != expected[Path(name)]}
    return fresh, different


def source_hashes(root):
    inputs = {root / 'CMakeLists.txt'}
    code_and_fixtures = {'.cpp', '.c', '.h', '.hpp', '.py', '.ps1', '.lua', '.ini', '.json', '.txt', '.nmap', '.glsl'}
    for directory in ('src', 'tests'):
        inputs.update(path for path in (root / directory).rglob('*')
                      if path.is_file() and path.suffix in code_and_fixtures)
    inputs.update(path for path in (root / 'tools').rglob('*.py') if path.is_file())
    for directory in ('assets', 'maps'):
        inputs.update(path for path in (root / directory).rglob('*')
                      if path.is_file() and path.suffix in ('.ini', '.json', '.lua', '.txt', '.map', '.nmap', '.glsl'))
    return {path.relative_to(root).as_posix(): digest(path) for path in sorted(inputs) if path.is_file()}


def cache_values(build):
    cache = build / 'CMakeCache.txt'
    if not cache.is_file():
        raise ValueError('Configure the build with CMake before running verification.')
    return {match[1]: match[2] for match in re.finditer(r'^([A-Za-z_][A-Za-z0-9_]*):[^=\r\n]+=(.*)$',
            cache.read_text(encoding='utf-8-sig'), re.MULTILINE)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=Path('build'))
    parser.add_argument('--configuration', default='Release')
    scope = parser.add_mutually_exclusive_group()
    scope.add_argument('--all', action='store_true')
    scope.add_argument('--tests', nargs='+')
    parser.add_argument('--list-tests', action='store_true')
    parser.add_argument('--require-tests', type=Path, help='JSON list captured before a build/test refactor')
    parser.add_argument('--state-baseline', type=Path, help='JSON mapping of relative state paths to SHA-256')
    parser.add_argument('--minimum-state-files', type=int, default=1)
    parser.add_argument('--output', type=Path, help='New directory for this run; existing evidence is never overwritten')
    parser.add_argument('--runtime-dir', type=Path, action='append', default=[])
    parser.add_argument('--cmake')
    parser.add_argument('--ctest')
    parser.add_argument('--no-build', action='store_true', help='Use an already-built executable explicitly')
    args = parser.parse_args()
    if not (args.list_tests or args.all or args.tests):
        parser.error('Choose --all, --tests or --list-tests.')
    if args.minimum_state_files < 1:
        parser.error('--minimum-state-files must be positive.')
    build = args.build.resolve()
    cache = cache_values(build)
    if cache.get('BUILD_TESTING', '').upper() not in ('ON', 'TRUE', 'YES', 'Y', '1'):
        raise ValueError('BUILD_TESTING must be ON; stale CTest files from an OFF configuration are not verification.')
    root = Path(cache['CMAKE_HOME_DIRECTORY'])
    cmake = args.cmake or cache.get('CMAKE_COMMAND') or shutil.which('cmake')
    ctest = args.ctest or cache.get('CMAKE_CTEST_COMMAND') or shutil.which('ctest')
    if not cmake or not ctest:
        raise ValueError('CMake/CTest was not found; supply --cmake and --ctest.')
    environment = os.environ.copy()
    for directory in reversed(args.runtime_dir):
        if not directory.is_dir():
            raise ValueError('Runtime directory does not exist: ' + str(directory))
        environment['PATH'] = str(directory.resolve()) + os.pathsep + environment.get('PATH', '')
    output = args.output or root / 'review_out' / ('verify-' + datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ-') + uuid.uuid4().hex[:6])
    if not args.list_tests:
        output.mkdir(parents=True, exist_ok=False)
    record = {'build': str(build), 'configuration': args.configuration, 'scope': 'all' if args.all else 'selection',
              'gatePassed': False, 'commands': [], 'buildRequested': not args.no_build,
              'startedUTC': datetime.datetime.now(datetime.timezone.utc).isoformat()}

    def run(command, log):
        start = time.monotonic()
        with log.open('wb') as stream:
            child = subprocess.Popen(command, cwd=root, env=environment, stdout=stream,
                                     stderr=subprocess.STDOUT, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            print(log.stem, 'pid', child.pid, flush=True)
            code = child.wait()
        record['commands'].append({'command': command, 'pid': child.pid, 'exitCode': code, 'seconds': time.monotonic() - start})
        return code

    def listing(pattern=None):
        command = [ctest, '--test-dir', str(build), '-C', args.configuration, '--show-only=json-v1']
        if pattern is not None:
            command += ['-R', pattern]
        result = subprocess.run(command, cwd=root, env=environment, capture_output=True, text=True, encoding='utf-8')
        if result.returncode:
            raise ValueError(result.stderr or 'CTest listing failed.')
        return json.loads(result.stdout)['tests']

    try:
        build_inputs = None
        if not args.list_tests and not args.no_build:
            build_inputs = source_hashes(root)
            if run([cmake, '--build', str(build), '--config', args.configuration], output / 'build.log'):
                raise ValueError('Build failed; inspect build.log.')
            if cache_values(build).get('BUILD_TESTING', '').upper() not in ('ON', 'TRUE', 'YES', 'Y', '1'):
                raise ValueError('The build disabled testing; stale CTest files are not verification.')
        registered = listing()
        names = {test['name'] for test in registered}
        if args.list_tests:
            print(json.dumps(sorted(names), ensure_ascii=False, indent=2))
            return 0
        if not names:
            raise ValueError('No tests are configured.')
        if args.require_tests:
            required = json.loads(args.require_tests.read_text(encoding='utf-8-sig'))
            if not isinstance(required, list) or any(not isinstance(name, str) for name in required):
                raise ValueError('--require-tests must contain a JSON list of names.')
            if set(required) - names:
                raise ValueError('Previously required tests disappeared: ' + ', '.join(sorted(set(required) - names)))
        if args.tests and set(args.tests) - names:
            raise ValueError('Unknown tests: ' + ', '.join(sorted(set(args.tests) - names)))
        pattern = None if args.all else '^(' + '|'.join(re.escape(name) for name in args.tests) + ')$'
        selected = registered if args.all else listing(pattern)
        expected = {test['name'] for test in selected}
        record.update(registeredTests=sorted(names), configuredTests=sorted(expected))
        executables = {Path(argument) for test in selected for argument in test.get('command', [])
                       if Path(argument).stem.lower().startswith('ra2') and Path(argument).is_file()}
        binaries = {str(path): digest(path) for path in executables}
        before_inputs = source_hashes(root)
        if build_inputs is not None and build_inputs != before_inputs:
            raise ValueError('Code/config inputs changed during the build; rebuild the final inputs before testing.')
        record['buildInputDigest'] = hashlib.sha256(json.dumps(build_inputs, sort_keys=True).encode()).hexdigest() if build_inputs is not None else None
        baseline = json.loads(args.state_baseline.read_text(encoding='utf-8-sig')) if args.state_baseline else {}
        states = state_candidates(baseline, root, build)
        before_states = state_times(states)
        command = [ctest, '--test-dir', str(build), '-C', args.configuration, '--output-on-failure', '-j', '1']
        if pattern is not None:
            command += ['-R', pattern]
        code = run(command, output / 'ctest.log')
        result = execution_result((output / 'ctest.log').read_text(encoding='utf-8-sig', errors='replace'), code, expected)
        fresh, different = compare_states(states, before_states)
        after_inputs = source_hashes(root)
        changed_inputs = sorted(name for name in before_inputs.keys() | after_inputs.keys()
                                if before_inputs.get(name) != after_inputs.get(name))
        changed_binaries = sorted(name for name, checksum in binaries.items()
                                  if not Path(name).is_file() or digest(Path(name)) != checksum)
        record.update(result)
        record.update(binarySHA256=binaries, changedBinaries=changed_binaries, codeAndConfigSHA256=before_inputs,
                      changedInputs=changed_inputs, freshStateOutputs=fresh, baselineDifferences=different)
        record['gatePassed'] = (result['gatePassed'] and not changed_inputs and not changed_binaries and not different
                                and (not args.state_baseline or len(fresh) >= args.minimum_state_files))
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        record['error'] = str(error)
    (output / 'result.json').write_text(json.dumps(record, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print('verification', 'PASS' if record['gatePassed'] else 'FAIL', 'executed', len(record.get('executedTests', [])),
          'fresh states', len(record.get('freshStateOutputs', {})), 'result', str(output / 'result.json'), flush=True)
    return 0 if record['gatePassed'] else 1


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(2)
