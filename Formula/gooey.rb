# frozen_string_literal: true

# Homebrew formula for Gooey – TheaterGWD Control Center
#
# Install (one line):
#   brew tap halfsohappy/theatergwd https://github.com/halfsohappy/TheaterGWD && brew install gooey
#
# Or step by step:
#   brew tap halfsohappy/theatergwd https://github.com/halfsohappy/TheaterGWD
#   brew install gooey
#
class Gooey < Formula
  desc "TheaterGWD Control Center – web-based OSC device manager for theater sensor hardware"
  homepage "https://github.com/halfsohappy/TheaterGWD"
  url "https://github.com/halfsohappy/TheaterGWD.git",
      branch: "main"
  version "2026.3.28.209"
  license "MIT"

  depends_on "python@3"

  def install
    # Copy gooey application into libexec
    libexec.install Dir["gooey/*"]

    # Bundle the docs so the in-app guide route works after install
    (libexec/"docs").install Dir["docs/*.md"]

    # Create a Python virtual environment
    venv_dir = libexec/"venv"
    system Formula["python@3"].opt_bin/"python3", "-m", "venv", venv_dir.to_s

    # Install Python dependencies
    system venv_dir/"bin/pip", "install", "--upgrade", "pip"
    system venv_dir/"bin/pip", "install", "-r", (libexec/"requirements.txt").to_s

    # Create the `gooey` launcher script
    (bin/"gooey").write <<~SH
      #!/bin/bash
      # Gooey – TheaterGWD Control Center
      # https://github.com/halfsohappy/TheaterGWD
      cd "#{libexec}" && exec "#{venv_dir}/bin/python" "#{libexec}/run.py" "$@"
    SH
  end

  def caveats
    <<~EOS
      Gooey – TheaterGWD Control Center is installed! 🎭

      Start it with:
        gooey

      Common options:
        gooey --port 8080       # Use a different port
        gooey --no-browser      # Don't auto-open the browser
        gooey --host 0.0.0.0    # Allow access from other devices
        gooey --debug           # Enable debug mode

      The control center will open at http://127.0.0.1:5000 by default.

      Documentation:
        https://github.com/halfsohappy/TheaterGWD/tree/main/gooey/docs
    EOS
  end

  test do
    assert_match "TheaterGWD", shell_output("#{bin}/gooey --help")
  end
end
