"""Art(md).ini infantry sequences: start, count, facing stride (not frame rate).

Format cross-check: Phobos-developers/YRpp InfantryTypeClass.h SubSequenceStruct.
"""

def parse_sequence(value):
    if not value:
        return None
    try:
        fields = value.split(";", 1)[0].split(",")
        start, count, stride = map(int, fields[:3])
    except (ValueError, TypeError):
        return None
    return (start, count, stride) if start >= 0 and count > 0 and stride >= 0 else None


def sequence_frame(sequence, facing, phase=0):
    start, count, stride = sequence
    if not 0 <= facing < 8 or not 0 <= phase < count:
        raise ValueError("infantry facing/phase out of range")
    return start + facing * stride + phase


def sequence_fits(sequence, frame_count, facings=8):
    return bool(sequence) and sequence_frame(sequence, facings-1, sequence[1]-1) < frame_count
