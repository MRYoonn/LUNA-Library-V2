"""Behavioral tests for exactness and missing-versus-wrong evaluation."""
import unittest
from tools.evaluate_goldset import score, FIELDS


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

    def test_si_requires_abstention(self):
        item = self.fixture(True)
        self.assertEqual(score([item], {'PMC1': {'documentKind': 'supporting_information'}})['exact'], 1)
        result = score([item], {'PMC1': {'documentKind': 'supporting_information', 'title': 'Parent title'}})
        self.assertEqual(result['fields']['title']['wrong'], 1)
        self.assertEqual(result['exact'], 0)
