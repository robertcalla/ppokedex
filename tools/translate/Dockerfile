# Offline English -> Brazilian Portuguese translation (Argos Translate model, run with CTranslate2).
FROM python:3.12-slim
RUN pip install --no-cache-dir ctranslate2==4.6.0 sentencepiece==0.2.0
ADD https://argos-net.com/v1/translate-en_pb-1_9.argosmodel /model.zip
RUN python -c "import zipfile; zipfile.ZipFile('/model.zip').extractall('/model')" && rm /model.zip
