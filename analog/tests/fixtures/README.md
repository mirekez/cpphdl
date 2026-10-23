# ADC linear-system regression

`adc_feedback_linear.txt` captures the four-slice electrical SAR bench's rejected
Newton system at the integration attempt following 80.421875 ns. The first line
contains the dimension and stamp count; then come the right-hand side and
`row column coefficient` triplets. Duplicate stamps add, as in the actual engine.

The 349-variable, 1478-stamp system is full rank but poorly conditioned. Its
row-normalized reciprocal condition estimate is about `3.85e-14`; the attempted
state correction reaches about `1.4e9`. Double-precision direct solves miss the
existing `1e-8` residual bound. A higher-precision diagnostic solves it more
accurately. The sparse regression permits an accurate solution or an explicit
accuracy rejection; structural rank loss and an accepted inaccurate solution
are failures. The integration retry contract is checked separately against an
independent RC response.

Fixture SHA-256:
`80e8f67dc51045d568f517bcb0962cfc788a002a7894c5dff9781c1f85e31c8a`.
