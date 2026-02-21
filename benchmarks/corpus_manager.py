import os
import urllib.request
import zipfile
import tarfile
import shutil

# Defines standard dataset URLs and filenames
CORPORA = {
    "calgary": {
        "url": "http://www.data-compression.info/files/corpora/calgarycorpus.zip",
        "type": "zip",
        "expected_size": 3141622
    },
    "canterbury": {
        "url": "http://corpus.canterbury.ac.nz/resources/cantrbry.tar.gz",
        "type": "targz",
        "expected_size": 2810784
    },
    "silesia": {
        "url": "https://sun.aei.polsl.pl/~sdeor/corpus/silesia.zip",
        "type": "zip",
        "expected_size": 211938580
    },
    "enwik8": {
        "url": "http://mattmahoney.net/dc/enwik8.zip",
        "type": "zip",
        "expected_size": 100000000
    },
    "enwik9": {
        "url": "http://mattmahoney.net/dc/enwik9.zip",
        "type": "zip",
        "expected_size": 1000000000
    }
}

import ssl

# Disable SSL verification for old academic servers
try:
    _create_unverified_https_context = ssl._create_unverified_context
except AttributeError:
    pass
else:
    ssl._create_default_https_context = _create_unverified_https_context

def download_and_extract(corpus_dir="corpus"):
    """Validates existence of corpora, downloads and extracts them if missing."""
    os.makedirs(corpus_dir, exist_ok=True)
    os.makedirs(os.path.join(corpus_dir, "custom"), exist_ok=True)
    
    for name, info in CORPORA.items():
        out_dir = os.path.join(corpus_dir, name)
        
        # Check if already populated
        if os.path.exists(out_dir) and len(os.listdir(out_dir)) > 0:
            print(f"[OK] {name} corpus already exists.")
            continue
            
        print(f"[DOWNLOADING] {name} corpus from {info['url']}...")
        os.makedirs(out_dir, exist_ok=True)
        archive_path = os.path.join(corpus_dir, f"{name}.archive")
        
        try:
            urllib.request.urlretrieve(info["url"], archive_path)
        except Exception as e:
            print(f"[ERROR] Failed to download {name}: {e}")
            continue
            
        print(f"[EXTRACTING] {name} corpus...")
        try:
            if info["type"] == "zip":
                with zipfile.ZipFile(archive_path, 'r') as z:
                    z.extractall(out_dir)
            elif info["type"] == "targz":
                with tarfile.open(archive_path, 'r:gz') as t:
                    t.extractall(out_dir)
                    
            os.remove(archive_path)
            print(f"[SUCCESS] {name} corpus ready in {out_dir}")
        except Exception as e:
            print(f"[ERROR] Failed to extract {name}: {e}")

def get_corpus_files(corpus_dir="corpus"):
    """Returns a dict mapping corpus_name -> list of absolute file paths."""
    datasets = {}
    if not os.path.exists(corpus_dir):
        return datasets
        
    for name in os.listdir(corpus_dir):
        path = os.path.join(corpus_dir, name)
        if os.path.isdir(path):
            files = []
            for item in os.listdir(path):
                file_path = os.path.join(path, item)
                if os.path.isfile(file_path):
                    files.append(os.path.abspath(file_path))
            if files:
                datasets[name] = files
                
    return datasets

if __name__ == "__main__":
    download_and_extract()
    ds = get_corpus_files()
    for name, files in ds.items():
        size = sum(os.path.getsize(f) for f in files)
        print(f"Corpus '{name}': {len(files)} files, {size / (1024*1024):.2f} MB")
