import json
import os
import requests
import re

TOKEN = "..."  # Replace with your actual token
JSON_FILE = R"data\models\models_directory.json"

REPO_ID = "Marksdo/WhisperMate"
API_URL = f"https://huggingface.co/api/models/{REPO_ID}"

headers = {"Authorization": f"Bearer {TOKEN}"}


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


def get_repo_files():
    response = requests.get(API_URL, headers=headers)
    if response.status_code == 200:
        return response.json().get("siblings", [])
    else:
        print(f"Error fetching repository data: {response.status_code}")
        return []


def create_friendly_name(file_name):
    # Remove file extension
    name_without_ext = os.path.splitext(file_name)[0]

    # Extract language code if present
    lang_code_match = re.search(r"[-_\.]([a-z]{2})(?:[-_]|$)", name_without_ext.lower())
    lang_code = lang_code_match.group(1) if lang_code_match else None

    # Translate language code if found
    if lang_code and lang_code in LANGUAGE_CODES:
        lang_name = LANGUAGE_CODES[lang_code]
        # Replace the language code with the full name
        name_without_ext = re.sub(
            f"[-_\.]{lang_code}(?:[-_]|$)",
            f" {lang_name} ",
            name_without_ext,
            flags=re.IGNORECASE,
        )
    else:
        lang_name = None

    if ".cantonese" in name_without_ext:
        name_without_ext = name_without_ext.replace(".cantonese", " Cantonese")
        lang_name = "Cantonese"
    if ".zhTW" in name_without_ext:
        name_without_ext = name_without_ext.replace(".zhTW", " Chinese")
        lang_name = "Traditional Chinese"
    if ".kyrgyz" in name_without_ext:
        name_without_ext = name_without_ext.replace(".kyrgyz", " Kyrgyz")
        lang_name = "Kyrgyz"

    # Split by underscores or hyphens and capitalize each word
    name_parts = name_without_ext.replace("_", " ").replace("-", " ").split()
    if "ggml" in name_parts:
        name_parts.remove("ggml")
        name_parts.insert(0, "Whisper")
    return " ".join(word.capitalize() for word in name_parts).strip() + " (Marksdo)", lang_name


def create_local_folder_name(file_name):
    return os.path.splitext(file_name)[0]


def load_existing_json():
    if os.path.exists(JSON_FILE):
        with open(JSON_FILE, "r") as f:
            return json.load(f)
    return {"models": []}


def save_json(data):
    with open(JSON_FILE, "w") as f:
        json.dump(data, f, indent=2)


existing_data = load_existing_json()
existing_models = {model["files"][0]["url"]: model for model in existing_data["models"]}

files = get_repo_files()
new_models_count = 0

for file in files:
    if file["rfilename"].endswith(".bin"):
        download_url = (
            f"https://huggingface.co/{REPO_ID}/resolve/main/{file['rfilename']}"
        )

        if download_url not in existing_models:
            friendly_name, lang_name = create_friendly_name(file["rfilename"])
            local_folder_name = create_local_folder_name(file["rfilename"])

            new_model = {
                "friendly_name": friendly_name,
                "local_folder_name": local_folder_name,
                "type": "MODEL_TYPE_TRANSCRIPTION",
                "files": [
                    {"url": download_url, "sha256": file.get("sha256", "").upper()}
                ],
                "extra": {
                    "language": lang_name,
                    "description": "This model is a part of the WhisperMate project. https://whispermate.app",
                    "source": "Hugging Face",
                },
            }

            existing_data["models"].append(new_model)
            existing_models[download_url] = new_model
            new_models_count += 1
            print(f"Added new model: {friendly_name}")

save_json(existing_data)
print(f"Total new models added: {new_models_count}")
print(f"Total models in JSON: {len(existing_data['models'])}")
print(f"Updated {JSON_FILE}")
