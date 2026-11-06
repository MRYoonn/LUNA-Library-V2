"""Behavioral tests for exactness and missing-versus-wrong evaluation."""
import unittest
from unittest.mock import patch
from urllib.error import HTTPError
import hashlib
import io
from pathlib import Path
import tempfile
from tools.evaluate_goldset import score, fetch, FIELDS


class GoldsetScoring(unittest.TestCase):
    def fixture(self, si=False):
        expected = {k: '' for k in FIELDS}
        expected.update(documentKind='supporting_information' if si else 'article')
        if not si:
            expected.update(title='A Chemical Study', authors='Final Author',
                            journal=['Chemical Science', 'Chem. Sci.'], year='2024',
                            volume='15', page='e12345', doi='10.1039/example')
        return dict(id='PMC1', publisher='RSC', expected=expected)

    def test_article_number_aliases_and_exactness(self):
        item = self.fixture()
        actual = dict(item['expected'], journal='Chem Sci', title='A chemical study')
        actual['article_number'] = actual.pop('page')
        result = score([item], {'PMC1': actual})
        self.assertEqual(result['exact'], 1)
        actual['title'] = 'A Chemical Study with extra body text'
        self.assertEqual(score([item], {'PMC1': actual})['fields']['title']['wrong'], 1)

    def test_missing_and_wrong_are_distinct(self):
        item = self.fixture()
        result = score([item], {'PMC1': {'title': 'Wrong title'}})
        self.assertEqual(result['fields']['title']['wrong'], 1)
        self.assertEqual(result['fields']['authors']['missing'], 1)
        self.assertEqual(result['exact'], 0)
        self.assertEqual(score([item], {})['fields']['documentKind']['missing'], 1)

    def test_identifier_punctuation_and_range_separators_are_significant(self):
        item = self.fixture()
        item['expected']['page'] = '1234–1256'
        actual = dict(item['expected'], journal='Chem. Sci.', page='1234-1256')
        self.assertEqual(score([item], {'PMC1': actual})['exact'], 1)
        actual.update(doi='101039example', page='12341256')
        result = score([item], {'PMC1': actual})
        self.assertEqual(result['fields']['doi']['wrong'], 1)
        self.assertEqual(result['fields']['page']['wrong'], 1)

    def test_si_requires_abstention(self):
        item = self.fixture(True)
        self.assertEqual(score([item], {'PMC1': {'documentKind': 'supporting_information'}})['exact'], 1)
        result = score([item], {'PMC1': {'documentKind': 'supporting_information', 'title': 'Parent title'}})
        self.assertEqual(result['fields']['title']['wrong'], 1)
        self.assertEqual(result['exact'], 0)


class GoldsetAcquisition(unittest.TestCase):
    payload = b'%PDF-1.7\nSynthetic acquisition fixture only\n'

    def fixture(self):
        return dict(id='PMC1', pdf_url='https://example.invalid/public.pdf',
                    sha256=hashlib.sha256(self.payload).hexdigest())

    @patch('tools.evaluate_goldset.time.sleep')
    @patch('tools.evaluate_goldset.urlopen')
    def test_transient_service_failure_retries_same_fixture_and_verifies_hash(self, opening, sleep):
        opening.side_effect = [HTTPError(self.fixture()['pdf_url'], 503, 'Unavailable', {}, None),
                               io.BytesIO(self.payload)]
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary)
            self.assertEqual(fetch([self.fixture()], destination), [])
            self.assertEqual((destination/'PMC1.pdf').read_bytes(), self.payload)
        self.assertEqual(opening.call_count, 2)
        self.assertEqual(opening.call_args_list[0].args[0].full_url,
                         opening.call_args_list[1].args[0].full_url)
        sleep.assert_called_once_with(1)

    @patch('tools.evaluate_goldset.time.sleep')
    @patch('tools.evaluate_goldset.urlopen')
    def test_missing_fixture_is_failure_without_retry_or_replacement(self, opening, sleep):
        opening.side_effect = HTTPError(self.fixture()['pdf_url'], 404, 'Not found', {}, None)
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary)
            self.assertEqual(fetch([self.fixture()], destination),
                             [dict(id='PMC1', error='HTTPError', httpStatus=404)])
            self.assertFalse((destination/'PMC1.pdf').exists())
        self.assertEqual(opening.call_count, 1)
        sleep.assert_not_called()

    @patch('tools.evaluate_goldset.urlopen')
    def test_checksum_mismatch_is_failure_without_accepting_bytes(self, opening):
        opening.return_value = io.BytesIO(self.payload+b'changed')
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary)
            self.assertEqual(fetch([self.fixture()], destination),
                             [dict(id='PMC1', error='ValueError')])
            self.assertFalse((destination/'PMC1.pdf').exists())
        self.assertEqual(opening.call_count, 1)
