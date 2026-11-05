# CAD: enclosure

SolidWorks source files for the 3D-printed enclosure, supplied by the project's designer. Designer credit: *add name here*.

## Files (`source/`)

| File | What it is | Notes |
|---|---|---|
| `agro-_MSME.SLDASM` | Full assembly | Open this first. It references the part files below. |
| `agri_product_casing.SLDPRT` | Main casing (box) | Name taken from the file. Confirm. |
| `agri_product_back_door.SLDPRT` | Back door / cover | Name taken from the file. Confirm. |
| `agri_product_holder.SLDPRT` | Holder | Probably the sensor holder on the side. Confirm. |
| `holder_nut.SLDPRT` | Nut for the holder | Name taken from the file. Confirm. |
| `tree_snapper.SLDPRT` | Unknown | Possibly the tree/club logo panel. Confirm. |

The descriptions come from the file names only. These are SolidWorks binary files and have not been opened or checked. Edit this table once the designer confirms what each part is.

## Opening the assembly

Keep all six files in the same folder. SolidWorks looks for the parts next to the assembly. If it asks for missing references, point it at this `source/` folder.

## Still needed from the designer

- `.stl` or `.3mf` for each part, so the box can be printed without SolidWorks. Put them in `cad/stl/`.
- `.step` export of the assembly, for people who use other CAD tools. Put it in `cad/step/`.
- Print settings: material (the box looks white, the handle green), layer height, infill, supports, and quantity of each part.
- Which part is the carry handle. It is visible in the photos but is not among these six files.
- A short note on fit: PCB and battery holder dimensions, screw sizes and the LCD and button cut-outs.

## Licence

Not chosen yet. Ask the designer before making this folder public.
