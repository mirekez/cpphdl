"""Remove the extra parser halving from the pinned Magic sidewall model.

The existing micron-to-lambda conversion already halves each edge's value.
Retain that conversion; qualify the two parser keywords with actual extracted
parallel-wire and shield fixtures before using this candidate in a flow.
"""
import hashlib


def apply(source):
    path = source / 'extract/ExtTech.c'
    before = path.read_bytes()
    if hashlib.sha256(before).hexdigest() != '20c427751a8f1c258781c43b56352059f250c89a548eaac40c630b54cd953086':
        raise ValueError('unreviewed Magic sidewall parser source')
    text = before.decode()
    for argument in (3, 5):
        original = f'capVal = 0.5 * aToCap(argv[{argument}]);'
        if text.count(original) != 1:
            raise ValueError('ambiguous Magic sidewall parser')
        text = text.replace(original, f'capVal = aToCap(argv[{argument}]);')
    # The older per-edge factor in ExtTechFinal must remain. Removing both
    # factors would double the PDK's intended coupling instead of restoring it.
    if text.count('ec->ec_cap *= 0.5;') != 1:
        raise ValueError('unreviewed Magic sidewall unit conversion')
    path.write_text(text)
    return dict(file='extract/ExtTech.c', before_sha256=hashlib.sha256(before).hexdigest(),
        after_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
        reason='remove duplicated parser halving; retain micron/lambda per-edge conversion')
