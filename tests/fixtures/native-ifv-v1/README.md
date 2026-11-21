# Scene34 / NativeIFV v1 preservation

These real World58 saves were written during the first IFV implementation,
before its actor burst intervals were fixed. Both hold a boarded engineer.
Their embedded NativeIFV1 definitions retain the historical 3..5 logic-tick
catalog defaults. Current exports use NativeIFV2 and convert the actor's source
frame intervals to 6..10 ticks. Loading a v1 save must not silently rewrite it.
