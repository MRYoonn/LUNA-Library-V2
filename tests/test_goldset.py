"""Behavioral tests for exactness and missing-versus-wrong evaluation."""
import unittest
from unittest.mock import patch
from urllib.error import HTTPError
from email.utils import formatdate
import hashlib
import io
from pathlib import Path
import tempfile
from tools.evaluate_goldset import score, fetch, FIELDS, aws_openaccess_pdf


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

    def test_audit_only_field_cannot_implicitly_classify_image_only_si_as_article(self):
        si = self.fixture(True)
        # Real Windows case: PMC10803759-SI1 has no extracted metadata in
        # normal mode, but LUNA_PUBLIC_GOLDSET_AUDIT supplies _titleAudit.
        blank = score([si], {'PMC1': {}})
        audited = score([si], {'PMC1': {'_titleAudit': {'authorRows': [], 'runs': []}}})
        self.assertEqual(blank['fields']['documentKind'], audited['fields']['documentKind'])
        self.assertEqual(audited['fields']['documentKind']['missing'], 1)
        self.assertEqual(audited['fields']['documentKind']['wrong'], 0)
        # A genuinely identified paper still gets the article default.
        article = self.fixture()
        identified = score([article], {'PMC1': {'doi': '10.1039/example',
                                                '_titleAudit': {'authorRows': []}}})
        self.assertEqual(identified['fields']['documentKind']['correct'], 1)



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

    @patch('tools.evaluate_goldset.time.sleep')
    @patch('tools.evaluate_goldset.urlopen')
    def test_mirror_zip_recovers_identical_pinned_article_not_new_pdf(self, opening, sleep):
        article = dict(id='PMC10025926',
            pdf_url='https://europepmc.org/api/getPdf?pmcid=PMC10025926',
            sha256=hashlib.sha256(self.payload).hexdigest())
        archive_bytes=io.BytesIO()
        import zipfile
        with zipfile.ZipFile(archive_bytes,'w') as zipped:
            zipped.writestr('PMC10025926.pdf',self.payload)
        bad=HTTPError(article['pdf_url'],500,'Server failure',{},None)
        opening.side_effect=[bad,bad,bad,io.BytesIO(archive_bytes.getvalue())]
        with tempfile.TemporaryDirectory() as temporary:
            target=Path(temporary)
            self.assertEqual(fetch([article],target),[])
            self.assertEqual((target/'PMC10025926.pdf').read_bytes(),self.payload)
        self.assertEqual(opening.call_count,4)
        self.assertIn('ftp.ebi.ac.uk', opening.call_args_list[3].args[0].full_url)
        self.assertEqual(opening.call_args_list[3].args[0].full_url,
            'https://ftp.ebi.ac.uk/pub/databases/pmc/pdf/OA/PMCxxxx1003/PMC10025926.zip')

    @patch('tools.evaluate_goldset.urlopen')
    def test_bad_supplement_archive_uses_hash_pinned_pmc_supplement(self, opening):
        article=dict(id='PMC10803759-SI1',
            pdf_url='https://www.ebi.ac.uk/europepmc/webservices/rest/PMC10803759/supplementaryFiles',
            archive_member='41467_2024_44824_MOESM2_ESM.pdf',
            sha256=hashlib.sha256(self.payload).hexdigest())
        opening.side_effect=[io.BytesIO(b'html not zip'),io.BytesIO(self.payload)]
        with tempfile.TemporaryDirectory() as temporary:
            directory=Path(temporary)
            self.assertEqual(fetch([article],directory),[])
            self.assertEqual((directory/'PMC10803759-SI1.pdf').read_bytes(),self.payload)
        self.assertIn('/bin/41467_2024_44824_MOESM2_ESM.pdf',
                      opening.call_args_list[1].args[0].full_url)

    @patch('tools.evaluate_goldset.urlopen')
    def test_mirror_different_pdf_sha_is_rejected_without_saving(self, opening):
        article=dict(id='PMC10025926',
            pdf_url='https://europepmc.org/api/getPdf?pmcid=PMC10025926',
            sha256=hashlib.sha256(self.payload).hexdigest())
        opening.return_value=io.BytesIO(self.payload+b'swapped')
        with tempfile.TemporaryDirectory() as temporary:
            directory=Path(temporary)
            failures=fetch([article],directory)
            self.assertEqual(failures[0]['id'],article['id'])
            self.assertIn('attempts',failures[0])
            self.assertFalse((directory/'PMC10025926.pdf').exists())

    @patch('tools.evaluate_goldset.urlopen')
    def test_official_pmc_2026_cloud_metadata_obeys_pinned_sha_and_host(self, opening):
        identifier='PMC10025926'
        item=dict(id=identifier,
            pdf_url=f'https://europepmc.org/api/getPdf?pmcid={identifier}',
            sha256=hashlib.sha256(self.payload).hexdigest())
        metadata=dict(pmcid=identifier,version=1,is_pmc_openaccess=True,
            pdf_url=f's3://pmc-oa-opendata/{identifier}.1/{identifier}.1.pdf?md5=123')
        opening.side_effect=[io.BytesIO(__import__('json').dumps(metadata).encode()),
                             io.BytesIO(self.payload)]
        self.assertEqual(aws_openaccess_pdf(item),self.payload)
        self.assertEqual(opening.call_args_list[1].args[0].full_url,
            f'https://pmc-oa-opendata.s3.amazonaws.com/{identifier}.1/{identifier}.1.pdf')

    @patch('tools.evaluate_goldset.urlopen')
    def test_official_pmc_cloud_supplement_requires_exact_member_and_sha(self, opening):
        identifier='PMC10803759'
        member='41467_2024_44824_MOESM2_ESM.pdf'
        item=dict(id=identifier+'-SI1',archive_member=member,
            sha256=hashlib.sha256(self.payload).hexdigest())
        metadata=dict(pmcid=identifier,version=1,is_pmc_openaccess=True,
            media_urls=[f's3://untrusted-external/{identifier}.1/{member}',
                        f's3://pmc-oa-opendata/{identifier}.1/{member}?md5=0123'])
        opening.side_effect=[io.BytesIO(__import__('json').dumps(metadata).encode()),
                             io.BytesIO(self.payload)]
        self.assertEqual(aws_openaccess_pdf(item),self.payload)
        self.assertEqual(opening.call_count,2)
        item['sha256']=hashlib.sha256(self.payload+b'tampered').hexdigest()
        opening.side_effect=[io.BytesIO(__import__('json').dumps(metadata).encode()),
                             io.BytesIO(self.payload),
                             HTTPError('test',404,'not found',{},None),
                             HTTPError('test',404,'not found',{},None)]
        with self.assertRaises(ValueError):
            aws_openaccess_pdf(item)

    def test_rate_limit_honors_seconds_and_http_date_retry_after(self):
        epoch = 1762477200
        for header in ('45', formatdate(epoch+45, usegmt=True)):
            with self.subTest(header=header), tempfile.TemporaryDirectory() as temporary:
                error = HTTPError(self.fixture()['pdf_url'], 429, 'Rate limited',
                                  {'Retry-After': header}, None)
                with patch('tools.evaluate_goldset.urlopen', side_effect=[error, io.BytesIO(self.payload)]) as opening, \
                     patch('tools.evaluate_goldset.time.sleep') as sleep, \
                     patch('tools.evaluate_goldset.time.time', return_value=epoch):
                    self.assertEqual(fetch([self.fixture()], Path(temporary)), [])
                    self.assertEqual(opening.call_count, 2)
                    sleep.assert_called_once_with(45)

    def test_long_service_cooldown_fails_without_premature_retry(self):
        error = HTTPError(self.fixture()['pdf_url'], 429, 'Rate limited',
                          {'Retry-After': '901'}, None)
        with tempfile.TemporaryDirectory() as temporary, \
             patch('tools.evaluate_goldset.urlopen', side_effect=error) as opening, \
             patch('tools.evaluate_goldset.time.sleep') as sleep:
            self.assertEqual(fetch([self.fixture()], Path(temporary)),
                             [dict(id='PMC1', error='HTTPError', httpStatus=429, retryAfter='901')])
            self.assertEqual(opening.call_count, 1)
            sleep.assert_not_called()
