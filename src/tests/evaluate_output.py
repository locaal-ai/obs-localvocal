import Levenshtein
import argparse
import unicodedata
import re
import difflib

def remove_accents(text):
    return ''.join(c for c in unicodedata.normalize('NFD', text)
                   if unicodedata.category(c) != 'Mn')

def clean_text(text):
    # Remove punctuation and special characters
    text = re.sub(r'[^\w\s]', '', text)
    # Remove extra whitespace
    text = re.sub(r'\s+', ' ', text).strip()
    return text

def normalize_spanish_gender_postfixes(text):
    # Normalize
    text = re.sub(r'\b(\w+?)(a)\b', r'\1e', text)
    return text

def tokenize(text, should_remove_accents=False, remove_punctuation=False):
    # Convert to lowercase, remove accents, clean text, and split
    if should_remove_accents:
        text = remove_accents(text)
        text = normalize_spanish_gender_postfixes(text)
    if remove_punctuation:
        text = clean_text(text)
    tokens = text.lower().split()
    return tokens

def calculate_wer(ref_text_tokens, hyp_text_tokens):
    distance = Levenshtein.distance(ref_text_tokens, hyp_text_tokens, weights=(1, 1, 1))
    wer = distance / max(len(ref_text_tokens), len(hyp_text_tokens))
    return wer

def calculate_cer(ref_text_tokens, hyp_text_tokens):
    # Join tokens into a single string
    ref_text = ' '.join(ref_text_tokens)
    hyp_text = ' '.join(hyp_text_tokens)
    distance = Levenshtein.distance(ref_text, hyp_text)
    cer = distance / len(ref_text)
    return cer

def print_alignment(ref_words, hyp_words):
    d = difflib.Differ()
    diff = list(d.compare(ref_words, hyp_words))
    
    print("\nToken-by-token alignment:")
    print("Reference | Hypothesis")
    print("-" * 30)
    
    ref_token = hyp_token = ""
    for token in diff:
        if token.startswith("  "):  # Common token
            if ref_token or hyp_token:
                print(f"{ref_token:<10} | {hyp_token:<10}")
                ref_token = hyp_token = ""
            print(f"{token[2:]:<10} | {token[2:]:<10}")
        elif token.startswith("- "):  # Token in reference, not in hypothesis
            ref_token = token[2:]
        elif token.startswith("+ "):  # Token in hypothesis, not in reference
            hyp_token = token[2:]
            if ref_token:
                print(f"{ref_token:<10} | {hyp_token:<10} (Substitution)")
                ref_token = hyp_token = ""
            else:
                print(f"{"":10} | {hyp_token:<10} (Insertion)")
                hyp_token = ""
    
    # Print any remaining tokens
    if ref_token:
        print(f"{ref_token:<10} | {"":10} (Deletion)")
    elif hyp_token:
        print(f"{"":10} | {hyp_token:<10} (Insertion)")


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
parser.add_argument('--remove_accents', action='store_true', help='Remove accents from text')
parser.add_argument('--remove_punctuation', action='store_true', help='Remove punctuation from text')
parser.add_argument('--print_alignment', action='store_true', help='Print the alignment to the console')
parser.add_argument('--write_tokens', action='store_true', help='Write the tokens to a file')
args = parser.parse_args()

ref_text = read_text_from_file(args.ref_file_path, join_sentences=True)
hyp_text = read_text_from_file(args.hyp_file_path, join_sentences=True)
ref_tokens = tokenize(ref_text, should_remove_accents=args.remove_accents, remove_punctuation=args.remove_punctuation)
hyp_tokens = tokenize(hyp_text, should_remove_accents=args.remove_accents, remove_punctuation=args.remove_punctuation)

if args.print_alignment:
    print_alignment(ref_tokens, hyp_tokens)

if args.write_tokens:
    with open("ref_tokens.txt", "w", encoding="utf-8") as file:
        file.write('\n'.join(ref_tokens))
    with open("hyp_tokens.txt", "w", encoding="utf-8") as file:
        file.write('\n'.join(hyp_tokens))

wer = calculate_wer(ref_tokens, hyp_tokens)

print(f"\"{args.ref_file_path}\" WER: \"{wer:.2}\"")

