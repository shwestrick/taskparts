# PBBS: taskparts vs parlay

Runs the PBBS benchmarks (`~/proj/pbbsbench`) with parlay's scheduler
replaced by taskparts. `taskparts.h` is a parlay scheduler plugin
(selected with `-DPARLAY_TASKPARTS`; add `-DTASKPARTS_MAINLINE` for the
`main` branch of taskparts). Each benchmark's `main` runs inside
`taskparts::launch`.

```
./setup.sh WORK                 # copies pbbsbench and parlaylib into WORK; touches nothing else
python3 build.py WORK           # builds <bench>.parlay, .taskparts-main, .taskparts-minimal
python3 run.py WORK --procs 1-10 --variants parlay taskparts-minimal
python3 plot.py WORK/results.csv WORK/plots
```

- parlaylib is taken at the commit pbbsbench pins as a submodule, since
  pbbsbench doesn't build against newer parlaylib.
- Each benchmark is compiled as one translation unit, because mainline
  taskparts defines globals in its headers.
- `run.py` uses the first input of each benchmark's `testInputs`, records
  every timed round, and checks outputs at the P values given by
  `--check-procs`. It resumes from an existing CSV.
- `rangeQuery2d` is skipped: it needs pbbsbench's PAM submodule.
