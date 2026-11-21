"""Preserve gate door frames and original minute-based timing, without new art."""
from decimal import Decimal
from PIL import Image
from ra2lib import shp_frame_to_rgba


def original_frames(minutes):
    # Westwood game minutes use 900 original logic frames. The runtime converts
    # this count to its own fixed clock; export must not round each frame anew.
    value=int(Decimal(minutes)*900)
    if not 0<=value<=900000:
        raise ValueError('gate time outside supported range')
    return value


def export_gate_frames(shp,palette,target,name,remap):
    count=shp.nframes//2
    for index in range(count):
        body=Image.new('RGBA',(shp.w,shp.h));mask=Image.new('L',body.size)
        shadow=shp.frame_pixels(index+count)
        if shadow.w and shadow.h:
            body.alpha_composite(shp_frame_to_rgba(shadow,palette,remap=False),(shadow.x,shadow.y))
        frame=shp.frame_pixels(index)
        if frame.w and frame.h:
            body.alpha_composite(shp_frame_to_rgba(frame,palette,remap=False),(frame.x,frame.y))
            if remap:
                for y in range(frame.h):
                    for x in range(frame.w):
                        value=frame.pixels[y*frame.w+x]
                        if 16<=value<=31:mask.putpixel((frame.x+x,frame.y+y),max(palette[value]))
        size=(round(shp.w*16/15),round(shp.h*16/15))
        body.resize(size,Image.Resampling.NEAREST).save(target/'actors'/f'{name}_{index}.png')
        if remap:mask.resize(size,Image.Resampling.NEAREST).save(target/'actors'/f'{name}_{index}_remap.png')
    return count
