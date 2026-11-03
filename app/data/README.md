# Preparation symbol metadata

`aerolib.inl` is the shadPS4 symbol-name database copied from the local reference
checkout's `src/core/aerolib/aerolib.inl`. Its original copyright and
GPL-2.0-or-later notice are preserved. It is used by `prepare_gravity.py` to
name imported APIs without requiring a separate reference checkout.

`missing_154_resolved.txt` contains the previously resolved NID-to-API-name
list used by the same preparation script. These are symbol names, not game
code or assets. Generated boot images stay in the ignored `app/out` folder.
