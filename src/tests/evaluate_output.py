import Levenshtein
import argparse
from diff_match_patch import diff_match_patch

def visualize_differences(ref_text, hyp_text):
    dmp = diff_match_patch()
    diffs = dmp.diff_main(hyp_text, ref_text, checklines=True)
    html = dmp.diff_prettyHtml(diffs)
    return html

def calculate_wer(ref_text, hyp_text):
    ref_words = ref_text.split()
    hyp_words = hyp_text.split()

    distance = Levenshtein.distance(ref_words, hyp_words)
    wer = distance / len(ref_words)
    return wer

def calculate_cer(ref_text, hyp_text):
    distance = Levenshtein.distance(ref_text, hyp_text)
    cer = distance / len(ref_text)
    return cer

def compare_tokens(ref_tokens, hyp_tokens):
    comparisons = []
    for ref_token, hyp_token in zip(ref_tokens, hyp_tokens):
        distance = Levenshtein.distance(ref_token, hyp_token)
        comparison = {'ref_token': ref_token, 'hyp_token': hyp_token, 'error_rate': distance / len(ref_token)}
        comparisons.append(comparison)
    return comparisons

def read_text_from_file(file_path, join_sentences=True):
    with open(file_path, 'r', encoding='utf-8', errors='ignore') as file:
        sentences = file.readlines()
    sentences = [sentence.strip() for sentence in sentences]
    # merge into a single string
    if join_sentences:
        return ' '.join(sentences)
    return sentences

parser = argparse.ArgumentParser(description='Evaluate output')
parser.add_argument('ref_file_path', type=str, help='Path to the reference file')
parser.add_argument('hyp_file_path', type=str, help='Path to the hypothesis file')
args = parser.parse_args()

ref_text = read_text_from_file(args.ref_file_path)
hyp_text = read_text_from_file(args.hyp_file_path)
wer = calculate_wer(ref_text, hyp_text)
cer = calculate_cer(ref_text, hyp_text)
print("Word Error Rate (WER):", wer)
print("Character Error Rate (CER):", cer)

ref_text = '\n'.join(read_text_from_file(args.ref_file_path, join_sentences=False))
hyp_text = '\n'.join(read_text_from_file(args.hyp_file_path, join_sentences=False))
html_diff = visualize_differences(ref_text, hyp_text)
with open("diff_visualization.html", "w", encoding="utf-8") as file:
    file.write(html_diff)

from Bio.Align import PairwiseAligner

aligner = PairwiseAligner()

alignments = aligner.align(ref_text, hyp_text)

# write the first alignment to a file
with open("alignment.txt", "w", encoding="utf-8") as file:
    file.write(alignments[0].format())

