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

def read_text_from_file(file_path):
    with open(file_path, 'r', encoding='utf-8', errors='ignore') as file:
        sentences = file.readlines()
    sentences = [sentence.strip() for sentence in sentences]
    # merge into a single string
    return ' '.join(sentences)

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

html_diff = visualize_differences(ref_text, hyp_text)
with open("diff_visualization.html", "w", encoding="utf-8") as file:
    file.write(html_diff)
