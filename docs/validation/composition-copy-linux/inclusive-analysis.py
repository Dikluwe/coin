#!/usr/bin/env python3
"""Describe measured classifier+transfer subtotals from archived raw diagnostics.

Reads the observation JSON only. No GPU/build/Git/executable is invoked. The
original classify/sort/finish pass includes the accumulated proof predicate;
its complete duration is not isolated incremental overhead. This subtotal is
separate from the transfer/lookup primary and from the total CSV frame time.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics

DETAIL = ('composition_detail.classify_ms', 'composition_detail.sort_ms', 'composition_detail.finish_ms')
TRANSFER = 'composition_transfer.copy_lookup_ms'


def numeric(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def compare(before, after):
    return {'before': before, 'after': after, 'delta': after-before,
            'change_percent': (after/before-1)*100 if before else None}


def derive(observation):
    processes, groups = {}, {}
    for directory, diagnostic in observation['diagnostics'].items():
        for stem, run in diagnostic['metadata_and_raw_trace']['runs'].items():
            records = run['command_metadata'];assert len(records)==1
            record = records[0];samples=run['samples'];row_count=samples['row_count']
            selection=samples['measured_row_indices'];assert samples['selection_valid'] and selection
            source_items=[run['trace_series'].get(key) for key in DETAIL]
            transfer=run.get('derived_series',{}).get(TRANSFER)
            aligned=all(item is not None and item.get('cardinality_matches_csv') and len(item['values'])==row_count and
                        all(numeric(value) for value in item['values']) for item in source_items+[transfer])
            entry={'directory':directory,'stem':stem,'variant':record['variant'],'case':record['case'],
                   'round':record['round'],'mode':record['mode'],'source_content_revision':record['source_content_revision'],
                   'csv_row_count':row_count,'measured_row_indices':selection,'warmup_row_indices':samples['warmup_row_indices'],
                   'source_series':{key:run['trace_series'].get(key) for key in DETAIL},
                   'transfer_series':transfer,'all_sources_cardinality_matches_csv':aligned}
            if aligned:
                classifier=[sum(item['values'][index] for item in source_items) for index in range(row_count)]
                inclusive=[classifier[index]+transfer['values'][index] for index in range(row_count)]
                entry.update(classifier_values_ms=classifier,inclusive_values_ms=inclusive,
                             classifier_measured_values_ms=[classifier[index] for index in selection],
                             inclusive_measured_values_ms=[inclusive[index] for index in selection],
                             classifier_measured_median_ms=statistics.median(classifier[index] for index in selection),
                             inclusive_measured_median_ms=statistics.median(inclusive[index] for index in selection))
            else:
                entry['limitation']='Unaligned/nonnumeric series retained; no descriptive measured subtotal inferred'
            processes[stem]=entry
            groups.setdefault(record['variant']+'|'+record['case'],{}).setdefault(record['mode'],[]).append(stem)
    comparisons={}
    for label,modes in sorted(groups.items()):
        pairs={};mode_data={}
        for mode,stems in modes.items():
            entries=sorted((processes[stem] for stem in stems),key=lambda item:item['round'])
            assert len(entries)==3 and {entry['round'] for entry in entries}=={1,2,3}
            summary={'processes':len(entries),'stems':[entry['stem'] for entry in entries],
                     'all_cardinalities_match':all(entry['all_sources_cardinality_matches_csv'] for entry in entries)}
            if summary['all_cardinalities_match']:
                for name in ('classifier','inclusive'):
                    values=[entry[name+'_measured_median_ms'] for entry in entries]
                    summary[name+'_process_measured_medians_ms']=values
                    summary[name+'_median_of_process_medians_ms']=statistics.median(values)
            mode_data[mode]=summary
        if set(mode_data)=={'on','off'} and all(value['all_cardinalities_match'] for value in mode_data.values()):
            for name in ('classifier','inclusive'):
                pairs[name+'_ms']=compare(mode_data['off'][name+'_median_of_process_medians_ms'],mode_data['on'][name+'_median_of_process_medians_ms'])
        comparisons[label]={'modes':mode_data,'comparisons':pairs}
    return {'source_keys':list(DETAIL)+[TRANSFER],'processes':processes,'group_comparisons':comparisons,
            'all_sources_cardinality_matches_csv':all(entry['all_sources_cardinality_matches_csv'] for entry in processes.values()),
            'process_count':len(processes),
            'protocol':{'selection':'CSV measured indices only; row-wise sum before selection; all raw source series preserved',
                        'aggregation':'median of three per-process measured medians in each on/off API/case group',
                        'classifier':'classify+sort+finish cover the existing order pass including accumulated proof and trace effects; not isolated new overhead',
                        'transfer':'Target/schedule copy+lookup primary excludes classification; schedule copy_ms includes complete literal schedule realization, not pure memcpy',
                        'inclusive':'descriptive classification plus transfer/lookup subtotal; distinct from measured total_ms and does not explain all of its variation'}}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();assert args.input.resolve()!=args.output.resolve()
    data=args.input.read_bytes();report=derive(json.loads(data))
    script=Path(__file__).resolve()
    report['provenance']={'input':{'path':str(args.input.resolve()),'sha256':hashlib.sha256(data).hexdigest()},
                          'script':{'path':str(script),'sha256':hashlib.sha256(script.read_bytes()).hexdigest()}}
    args.output.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    print('Descriptive inclusive subtotal:',report['process_count'],'processes; aligned',report['all_sources_cardinality_matches_csv'])


if __name__=='__main__':main()
