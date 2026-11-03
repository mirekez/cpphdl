"""Fetch the pinned OSDI ABI header; generated/downloaded files stay in out/."""
import argparse
import hashlib
from pathlib import Path
import ssl
import urllib.request

URL = ('https://raw.githubusercontent.com/OpenVAF/OpenVAF-Reloaded/'
       '9339495ac799afe1beca6d9d703c7b209fe54b08/openvaf/osdi/header/osdi_0_3.h')
SHA256 = '6439631155ff0f6447428b65737aac4421216a067c687d74cb42b2d5a1910a24'


def prepare(directory):
    header = directory / 'osdi.h'
    if header.exists() and hashlib.sha256(header.read_bytes()).hexdigest() == SHA256:
        return header
    context = ssl.create_default_context(cafile='/etc/ssl/certs/ca-certificates.crt')
    data = urllib.request.urlopen(URL, context=context, timeout=30).read()
    if hashlib.sha256(data).hexdigest() != SHA256:
        raise ValueError('OSDI SDK header hash mismatch')
    directory.mkdir(parents=True, exist_ok=True)
    header.write_bytes(data)
    return header


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[2] / 'out/osdi-sdk')
    print(prepare(parser.parse_args().output))
