#!/bin/sh
# Runs every quest scenario in shots/q/ and summarises the PASS/FAIL checks.
cd "$(dirname "$0")/.."
fails=0
for q in cook sheep ghost rj doric rune imp witch goblin ernest pirate bkf demon; do
    python3 tools/qtest.py shots/q/$q.src shots/q/$q.txt
    out=$(python3 tools/run_scenario.py shots/q/$q.txt)
    p=$(echo "$out" | grep -c '^PASS')
    f=$(echo "$out" | grep -c '^FAIL')
    t=$(echo "$out" | grep -c 'timed out')
    printf '%-8s pass %2d  fail %d  timeouts %d\n' "$q" "$p" "$f" "$t"
    fails=$((fails + f))
done
# save mid-quest, reload, continue
rm -f shots/q/mid.sav
python3 tools/qtest.py shots/q/save1.src shots/q/save1.txt
python3 tools/qtest.py shots/q/save2.src shots/q/save2.txt
out=$( (python3 tools/run_scenario.py shots/q/save1.txt shots/q/mid.sav; python3 tools/run_scenario.py shots/q/save2.txt shots/q/mid.sav) )
printf '%-8s pass %2d  fail %d\n' save "$(echo "$out" | grep -c '^PASS')" "$(echo "$out" | grep -c '^FAIL')"
exit $fails
