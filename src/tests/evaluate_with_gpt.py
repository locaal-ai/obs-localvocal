'''
This script uses the OpenAI API to score translations from Korean to Japanese.
The translations are read from a file and the scores are written to another file.
The script sends the translations in batches of 5 to the API to get the scores.

The script reads the translations from a file called 'translation_pairs.txt' in the same directory.
The file should contain one translation pair per line, separated by a '|'.
The translations should be in the format 'Korean sentence | Japanese sentence'.
The script writes the scores to a file called 'translations_scores.txt' in the same directory.
The scores are written one per line in the same order as the translations.

The script uses the OpenAI API to score the translations.
The API key should be stored in an environment variable called OPENAI_API_KEY.

The script also defines a list of scored examples to help the evaluator understand the scoring system.
The examples are tuples of the form (Korean sentence, Japanese sentence, score).

Usage:
    python evaluate_with_gpt.py
'''

from openai import OpenAI
client = OpenAI()

# Define the ground truth and translations with their scores
scored_examples = [
    ("왜 자꾸 은행에서 알림이 오는 거야", "なぜ、銀行から連絡が来てるんだ？", 5),
    ("뭔지 모르겠어 그게", "何か分からない", 5),
    ("개 미친", "犬狂いなんだよ。", 5),
    ("아 왜지?", "なぜだろう？" ,5),
    ("나 맨날 맨날 목표 세워 놓을 거야", "私は毎日、いつもゴールを設定する。", 4),
    ("돈 들어올 것도 없는데", "収入はないのに、お金が集まるわけもなく。", 4),
    ("아니 너 자꾸 그러면 나", "いや、あなたは常にそう言って私がする。", 4),
    ("맨날 맨날 목표 세워논다", "毎日、いつもゴールを設定してるんだ。", 4),
    ("알림 같은 거 잘해놔", "警告のようなものをよく作っておけ。", 4),
    ("아 왜 안, 왜 근데 나오다 말지?", "なぜ出てこないのか？", 4),
    ("기억나 포로야", "覚えてるかい、ポーロヤ。", 4),
    ("DHL사에서 오는", "DHL4から来た...", 4),
    ("어 맞아 2개월", "正解2ヶ月...", 4),
    ("그니까 왜 디기 것만 음소거 내가 방금 켰어", "だからなぜディギーはたったのを消すことにするんですか?", 3),
    ("포로가 보낸 게 아니래 그래서 뭐야 도대체", "捕虜が送ったのではないからな", 3),
    ("당연하죠 저는", "もちろんですよ。私はそう思いますが、", 3),
    ("근데 아마", "でも、おそらくは違うと思います。", 3),
    ("아 환불", "返金はできません。", 3),
    ("알림 같은 거 잘해놓고", "注意報などをよく設定して、気に入ったものはすぐ知らせておきましょう。", 2),
    ("돌림판 돌려서", "回転板を曲げて、旋盤で作る。", 2),
    ("그치", "でも、それは別にいいことではない。", 2),
    ("안뇽", "アンヌン・ドゥーヴァル", 2),
    ("어 맞아 2개월", "2ヶ月前に、私は彼女と結婚した。", 1),
    ("2개월 강매시켜 맞아 걔네", "2ヶ月強姦してやった", 1)
]

# Function to read translation pairs from a file
def read_translation_pairs(filename):
    with open(filename, 'r', encoding='utf-8') as file:
        pairs = file.readlines()
    # split the pairs into tuples
    pairs = [pair for pair in pairs]
    pairs = [pair.split('|') for pair in pairs]
    pairs = [tuple([k.strip(), j.strip()]) for k, j in pairs]
    return pairs

# Function to create a prompt for scoring translations
def create_system_prompt(scored_examples):
    prompt = "You are a translation evaluator. You will be asked to score the quality of " + \
        "Korean-to-Japanese translations on a scale from 1 to 5. " + \
        "The score should reflect how well the Japanese translation captures the meaning of the " +\
        "Korean sentence. " + \
        "1 means the translation is very poor, 5 means the translation is excellent. " + \
        "Don't pay attention to punctuation marks or whitespace, focus on the meaning.\n"
    prompt += "Here are examples of scoring:\n"
    for example in scored_examples:
        prompt += f"'{example[0]}' | '{example[1]}' | {example[2]}\n"
    prompt += "\n\n" + \
        "Proivde *only the scores* for all of the new examples. " + \
        "Translations which say 'N/A' are to be scored with 0. " + \
        "In one line each write a number between 0 and 5. " + \
        "Do not include any information other than the numerical 0-5 score in your response.\n\n"
    return prompt

def create_prompt(new_examples):
    prompt = f"Here are {len(new_examples)} translations without scores that need scoring:\n"
    for new_example in new_examples:
        prompt += f"'{new_example[0]}' | '{new_example[1]}'\n"
    prompt += f"Provide a score for each of the {len(new_examples)} translations."
    return prompt

# Function to score translations using OpenAI API
def score_translations(prompt_, system_prompt_):
    completion = client.chat.completions.create(
        model="gpt-4o",
        messages=[
            {"role": "system", "content": system_prompt_},
            {"role": "user", "content": prompt_}
        ],
        max_tokens=100,
    )
    return completion.choices[0].message.content.strip()

# Read the translation pairs from the file
translation_pairs = read_translation_pairs('translation_pairs.txt')

system_prompt = create_system_prompt(scored_examples)

# truncate the output file
with open('translations_scores.txt', 'w', encoding='utf-8') as file:
    file.write('')

# Get the scores from the OpenAI API in batches of 10
for i in range(0, len(translation_pairs), 5):
    prompt = create_prompt(translation_pairs[i:i+5])
    scores = score_translations(prompt, system_prompt).strip().split('\n')
    print(f'batch {i//5} done')
    # Save the results to a file
    with open('translations_scores.txt', 'a', encoding='utf-8') as file:
        file.write('\n'.join(scores) + '\n')
