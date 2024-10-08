import re
import requests
import time
import json
import os

API_URL = "https://huggingface.co/api/models"
TOKEN = "..."  # Replace with your actual token
JSON_FILE = R"data\models\models_directory.json"

headers = {"Authorization": f"Bearer {TOKEN}"}
params = {
    "search": "whisper gg",
    "sort": "downloads",
    "direction": -1,
    "limit": 500  # Maximum allowed per page
}

LANGUAGE_CODES = {
    "ar": "Arabic",
    "bg": "Bulgarian",
    "ca": "Catalan",
    "cs": "Czech",
    "da": "Danish",
    "de": "German",
    "el": "Greek",
    "en": "English",
    "es": "Spanish",
    "et": "Estonian",
    "fi": "Finnish",
    "fr": "French",
    "hi": "Hindi",
    "hr": "Croatian",
    "hu": "Hungarian",
    "id": "Indonesian",
    "is": "Icelandic",
    "it": "Italian",
    "iw": "Hebrew",
    "ja": "Japanese",
    "ko": "Korean",
    "lt": "Lithuanian",
    "lv": "Latvian",
    "ms": "Malay",
    "nl": "Dutch",
    "no": "Norwegian",
    "pl": "Polish",
    "pt": "Portuguese",
    "ro": "Romanian",
    "ru": "Russian",
    "sk": "Slovak",
    "sl": "Slovenian",
    "sr": "Serbian",
    "sv": "Swedish",
    "th": "Thai",
    "tr": "Turkish",
    "uk": "Ukrainian",
    "vi": "Vietnamese",
    "zh": "Chinese",
    # Added languages
    "nb": "Norwegian Bokmål",
    "si": "Sinhala",
    "uz": "Uzbek",
    "pa": "Punjabi",
    "ky": "Kyrgyz",
    "fa": "Persian",
    "dv": "Dhivehi",
    "yue": "Cantonese",
    "be": "Belarusian",
    "sw": "Swahili",
    "ne": "Nepali",
    "kn": "Kannada",
    "ps": "Pashto",
    "te": "Telugu",
    "ta": "Tamil",
    "ml": "Malayalam",
    "he": "Hebrew",
    "np": "Nepali",
    "gu": "Gujarati",
    "bn": "Bengali",
    "yo": "Yoruba",
    "ig": "Igbo",
    "ha": "Hausa",
    "am": "Amharic",
    # Special cases
    "zhTW": "Traditional Chinese",
    "jp": "Japanese",
    "kyrgyz": "Kyrgyz",
    "tamil": "Tamil",
    "telugu": "Telugu",
}

def get_model_details(model_id):
    model_url = f"https://huggingface.co/api/models/{model_id}"
    response = requests.get(model_url, headers=headers)
    if response.status_code == 200:
        return response.json()
    return None

def extract_bin_info(model_id, siblings):
    for file in siblings:
        if file['rfilename'].endswith(('.bin', '.gguf')):
            return {
                'filename': file['rfilename'],
                'download_url': f"https://huggingface.co/{model_id}/resolve/main/{file['rfilename']}",
                'sha256': file.get('sha256')
            }
    return None

def create_friendly_name(model_id):
    # Split the model_id by '/' and take the last part
    model_name = model_id.split('/')[-1]
    repo_name = model_id.split('/')[0]

        # Extract language code if present
    lang_code_match = re.search(r"[-_\.]([a-z]{2})(?:[-_\.]|$)", model_name.lower())
    lang_code = lang_code_match.group(1) if lang_code_match else None

    # Translate language code if found
    if lang_code and lang_code in LANGUAGE_CODES:
        lang_name = LANGUAGE_CODES[lang_code]
        # Replace the language code with the full name
        model_name = re.sub(
            f"[-_\.]{lang_code}(?:[-_]|$)",
            f" {lang_name} ",
            model_name,
            flags=re.IGNORECASE,
        )
    else:
        lang_name = None

    # Split by underscores or hyphens and capitalize each word
    name_parts = model_name.replace("_", " ").replace("-", " ").split()
    if "ggml" in name_parts:
        name_parts.remove("ggml")
    return " ".join(word.capitalize() for word in name_parts).strip() + f" ({repo_name})", lang_name

def create_local_folder_name(file_name):
    return os.path.splitext(file_name)[0]

def load_existing_json():
    if os.path.exists(JSON_FILE):
        with open(JSON_FILE, 'r') as f:
            return json.load(f)
    return {"models": []}

def save_json(data):
    with open(JSON_FILE, 'w') as f:
        json.dump(data, f, indent=2)

existing_data = load_existing_json()
existing_models = {model['files'][0]['url']: model for model in existing_data['models']}

cursor = None
total_models = 0

response = requests.get(API_URL, headers=headers, params=params)
if response.status_code != 200:
    print(f"Error: {response.status_code}")
    exit()

data = response.json()
models = data
total_models += len(models)

for model in models:
    model_id = model['id']
    details = get_model_details(model_id)
    if details and 'siblings' in details:
        bin_info = extract_bin_info(model_id, details['siblings'])
        if bin_info:
            if bin_info['download_url'] not in existing_models:
                friendly_name, lang_name = create_friendly_name(model_id)
                local_folder_name = create_local_folder_name(bin_info['filename'])
                new_model = {
                    "friendly_name": friendly_name,
                    "local_folder_name": local_folder_name,
                    "type": "MODEL_TYPE_TRANSCRIPTION",
                    "files": [
                        {
                            "url": bin_info['download_url'],
                            "sha256": bin_info['sha256'].upper() if bin_info['sha256'] else None
                        }
                    ],
                    "extra": {
                        "language": lang_name,
                        "description": "Whisper GGML model",
                        "source": "Hugging Face",
                    },
                }
                existing_data['models'].append(new_model)
                existing_models[bin_info['download_url']] = new_model
                print(f"Added new model: {friendly_name}")

print(f"Processed {total_models} models so far. Found {len(existing_data['models'])} matching models.")

# save_json(existing_data)
print(f"Total models processed: {total_models}")
print(f"Total matching models in JSON: {len(existing_data['models'])}")
print(f"Updated {JSON_FILE}")
