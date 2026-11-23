# Builds main/web/index.html (embedded in the firmware) from index.src.html, putting the Audiowide
# subset (assets/audiowide_call.ttf, SIL OFL 1.1) inline. Run after editing index.src.html.
import base64, pathlib
root = pathlib.Path(__file__).resolve().parent.parent
src = (root / 'main/web/index.src.html').read_text()
font = base64.b64encode((root / 'assets/audiowide_call.ttf').read_bytes()).decode()
(root / 'main/web/index.html').write_text(src.replace('@@FONT@@', font))
