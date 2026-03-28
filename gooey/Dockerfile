FROM python:3.12-slim

WORKDIR /app

# Install dependencies first (cached layer)
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Copy application
COPY . .

# Create show library directory
RUN mkdir -p data/shows

# Run as non-root
RUN useradd -m gooey && chown -R gooey:gooey /app
USER gooey

EXPOSE 5000

# Mount for show persistence
VOLUME ["/app/data/shows"]

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD python -c "import urllib.request; urllib.request.urlopen('http://localhost:5000/')"

CMD ["python", "run.py", "--host", "0.0.0.0", "--no-browser"]
