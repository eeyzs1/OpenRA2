import hashlib
from pathlib import Path
import tempfile
import unittest
import subprocess
import sys

from verify import cache_values, compare_states, execution_result, source_hashes, state_candidates, state_times


class VerificationRunnerTests(unittest.TestCase):
    def test_scripts_fixtures_maps_and_non_pack_tools_are_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            names = ('assets/scripts/events.lua', 'tests/fixtures/mod/rules.ini',
                     'tests/fixtures/mod/ai.lua', 'maps/reference.txt', 'tools/verify_launch_bats.py')
            for name in names:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('before')
            before = source_hashes(root)
            self.assertEqual(set(before), set(names))
            (root / names[0]).write_text('after')
            self.assertNotEqual(before[names[0]], source_hashes(root)[names[0]])

    def test_cache_keys_do_not_absorb_blank_lines(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'CMakeCache.txt').write_text('# CMake cache\n\nBUILD_TESTING:BOOL=OFF\n\nCMAKE_HOME_DIRECTORY:INTERNAL=E:/Project\n')
            self.assertEqual(cache_values(root), {'BUILD_TESTING': 'OFF', 'CMAKE_HOME_DIRECTORY': 'E:/Project'})

    def test_testing_off_is_rejected_before_stale_ctest_data_can_run(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'CMakeCache.txt').write_text('BUILD_TESTING:BOOL=OFF\n')
            (root / 'CTestTestfile.cmake').write_text('add_test(stale missing-old-executable)\n')
            result = subprocess.run([sys.executable, '-B', str(Path(__file__).with_name('verify.py')), '--build', str(root), '--list-tests'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn('BUILD_TESTING must be ON', result.stderr)

    def test_exit_zero_without_executed_tests_is_not_success(self):
        result = execution_result('No tests were found!!!\n', 0, {'ra2.state'})
        self.assertFalse(result['gatePassed'])

    def test_passed_selection_including_fixture_is_counted(self):
        text = ('1/2 Test #1: ra2.fixture ........ Passed 0.1 sec\n'
                '2/2 Test #2: ra2.state ........ Passed 0.2 sec\n'
                '100% tests passed, 0 tests failed out of 2\n')
        self.assertTrue(execution_result(text, 0, {'ra2.fixture', 'ra2.state'})['gatePassed'])

    def test_skipped_test_does_not_satisfy_the_selection(self):
        text = ('1/2 Test #1: ra2.fixture ........ Passed 0.1 sec\n'
                '2/2 Test #2: ra2.state ........ ***Skipped 0.2 sec\n'
                '100% tests passed, 0 tests failed out of 2\n')
        self.assertFalse(execution_result(text, 0, {'ra2.fixture', 'ra2.state'})['gatePassed'])

    def test_failed_test_without_space_after_progress_dots_is_counted(self):
        text = ('1/1 Test #34: ra2.ai_attack ....................***Failed    4.66 sec\n'
                '0% tests passed, 1 tests failed out of 1\n')
        result = execution_result(text, 8, {'ra2.ai_attack'})
        self.assertFalse(result['gatePassed'])
        self.assertEqual(result['executedTests'], ['ra2.ai_attack'])
        self.assertTrue(result['statuses']['ra2.ai_attack'].startswith('***Failed'))

    def test_partial_summary_does_not_satisfy_the_selection(self):
        text = ('1/1 Test #1: ra2.state ........ Passed 0.2 sec\n'
                '100% tests passed, 0 tests failed out of 2\n')
        self.assertFalse(execution_result(text, 0, {'ra2.state'})['gatePassed'])

    def test_nonzero_exit_cannot_be_hidden_by_passed_text(self):
        text = ('1/1 Test #1: ra2.state ........ Passed 0.2 sec\n'
                '100% tests passed, 0 tests failed out of 1\n')
        self.assertFalse(execution_result(text, 8, {'ra2.state'})['gatePassed'])

    def test_fresh_asan_output_uses_the_release_logical_path(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contents = b'RA2WLD60-state'
            baseline = {'build/audit/state.sav': hashlib.sha256(contents).hexdigest()}
            expected = state_candidates(baseline, root, root / 'build-asan')
            before = state_times(expected)
            target = root / 'build-asan/audit/state.sav'
            target.parent.mkdir(parents=True)
            target.write_bytes(contents)
            fresh, different = compare_states(expected, before)
            self.assertEqual(fresh, {str(target): baseline['build/audit/state.sav']})
            self.assertFalse(different)

    def test_stale_state_is_excluded_and_changed_fresh_state_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / 'build/audit/state.sav'
            target.parent.mkdir(parents=True)
            target.write_bytes(b'old')
            expected = state_candidates({'build/audit/state.sav': hashlib.sha256(b'old').hexdigest()}, root, root / 'build')
            before = state_times(expected)
            self.assertEqual(compare_states(expected, before), ({}, {}))
            target.write_bytes(b'changed')
            # Some filesystems coalesce timestamps; explicitly make this a new write.
            import os
            os.utime(target, ns=(before[target] + 1_000_000, before[target] + 1_000_000))
            fresh, different = compare_states(expected, before)
            self.assertIn(str(target), fresh)
            self.assertIn(str(target), different)


if __name__ == '__main__':
    unittest.main()
