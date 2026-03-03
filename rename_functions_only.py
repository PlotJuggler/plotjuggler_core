#!/usr/bin/env python3
"""
Carefully rename only function/method names from snake_case to camelCase.
Preserves parameter names, struct members, and variables in snake_case.
"""

import re
import os
from pathlib import Path

# Complete mapping of function/method names to rename
# Format: old_name -> new_name
FUNCTION_RENAMES = {
    # pj_base
    'numeric_type_size': 'numericTypeSize',
    'numeric_value_type': 'numericValueType',
    'numeric_value_to_double': 'numericValueToDouble',
    'make_primitive': 'makePrimitive',
    'make_struct': 'makeStruct',
    'make_array': 'makeArray',
    'make_enum': 'makeEnum',
    'flatten_field_paths': 'flattenFieldPaths',
    'count_leaf_fields': 'countLeafFields',
    'ok_status': 'okStatus',

    # buffer.hpp / buffer.cpp
    'bytes_for_bits': 'bytesForBits',
    'init_valid': 'initValid',
    'ensure_size': 'ensureSize',
    'set_valid': 'setValid',
    'set_null': 'setNull',
    'is_valid': 'isValid',
    'count_nulls': 'countNulls',
    'assign_bytes': 'assignBytes',
    'bit_span': 'bitSpan',
    'size_bytes': 'sizeBytes',
    'size_bits': 'sizeBits',

    # column_buffer
    'storage_kind_of': 'storageKindOf',
    'storage_kind_size': 'storageKindSize',
    'row_count': 'rowCount',
    'has_nulls': 'hasNulls',
    'append_float32': 'appendFloat32',
    'append_float64': 'appendFloat64',
    'append_int32': 'appendInt32',
    'append_int64': 'appendInt64',
    'append_uint64': 'appendUint64',
    'append_bool': 'appendBool',
    'append_string': 'appendString',
    'append_null': 'appendNull',
    'read_float32': 'readFloat32',
    'read_float64': 'readFloat64',
    'read_int32': 'readInt32',
    'read_int64': 'readInt64',
    'read_uint64': 'readUint64',
    'read_bool': 'readBool',
    'read_string': 'readString',
    'is_null': 'isNull',
    'read_as_double': 'readAsDouble',
    'append_float32_bulk': 'appendFloat32Bulk',
    'append_float64_bulk': 'appendFloat64Bulk',
    'append_int32_bulk': 'appendInt32Bulk',
    'append_int64_bulk': 'appendInt64Bulk',
    'append_uint64_bulk': 'appendUint64Bulk',
    'append_bool_bulk': 'appendBoolBulk',
    'append_strings_bulk': 'appendStringsBulk',
    'append_validity_bulk': 'appendValidityBulk',
    'value_buffer': 'valueBuffer',
    'validity_buffer': 'validityBuffer',
    'offsets_buffer': 'offsetsBuffer',
    'ensure_validity_initialized': 'ensureValidityInitialized',
    'append_fixed': 'appendFixed',
    'append_fixed_bulk': 'appendFixedBulk',
    'read_fixed': 'readFixed',

    # chunk.hpp / chunk.cpp
    'read_timestamp': 'readTimestamp',
    'read_timestamps': 'readTimestamps',
    'read_numeric_as_double': 'readNumericAsDouble',
    'read_column_as_doubles': 'readColumnAsDoubles',
    'begin_row': 'beginRow',
    'set_float32': 'setFloat32',
    'set_float64': 'setFloat64',
    'set_int32': 'setInt32',
    'set_int64': 'setInt64',
    'set_uint64': 'setUint64',
    'set_bool': 'setBool',
    'set_string': 'setString',
    'finish_row': 'finishRow',
    'append_timestamps': 'appendTimestamps',
    'append_column_float32': 'appendColumnFloat32',
    'append_column_float64': 'appendColumnFloat64',
    'append_column_int32': 'appendColumnInt32',
    'append_column_int64': 'appendColumnInt64',
    'append_column_uint64': 'appendColumnUint64',
    'append_column_bool': 'appendColumnBool',
    'append_column_strings': 'appendColumnStrings',
    'append_column_validity': 'appendColumnValidity',
    'finish_bulk_append': 'finishBulkAppend',
    'remaining_capacity': 'remainingCapacity',
    'is_full': 'isFull',
    'is_row_in_progress': 'isRowInProgress',
    'last_timestamp': 'lastTimestamp',
    'update_column_stats': 'updateColumnStats',
    'compute_bulk_numeric_stats': 'computeBulkNumericStats',
    'compute_bulk_string_stats': 'computeBulkStringStats',

    # topic_storage
    'append_sealed_chunk': 'appendSealedChunk',
    'evict_before': 'evictBefore',
    'clear_chunks': 'clearChunks',
    'sealed_chunks': 'sealedChunks',
    'set_column_descriptors': 'setColumnDescriptors',
    'column_descriptors': 'columnDescriptors',
    'update_schema': 'updateSchema',
    'update_max_observed_array_length': 'updateMaxObservedArrayLength',
    'increment_truncated_sample_count': 'incrementTruncatedSampleCount',
    'max_observed_array_length': 'maxObservedArrayLength',
    'truncated_sample_count': 'truncatedSampleCount',
    'array_expansion_count': 'arrayExpansionCount',
    'set_array_expansion_count': 'setArrayExpansionCount',

    # encoding
    'dictionary_encode_strings': 'dictionaryEncodeStrings',
    'dictionary_lookup': 'dictionaryLookup',
    'pack_bools': 'packBools',
    'unpack_bool': 'unpackBool',
    'constant_encode': 'constantEncode',
    'constant_decode_as_double': 'constantDecodeAsDouble',
    'for_encode': 'forEncode',
    'for_decode_one_as_double': 'forDecodeOneAsDouble',
    'for_decode_range_as_doubles': 'forDecodeRangeAsDoubles',
    'index_bytes_for': 'indexBytesFor',
    'offset_bytes_for': 'offsetBytesFor',

    # engine
    'create_dataset': 'createDataset',
    'get_dataset': 'getDataset',
    'create_topic': 'createTopic',
    'get_topic_storage': 'getTopicStorage',
    'type_registry': 'typeRegistry',
    'create_time_domain': 'createTimeDomain',
    'get_time_domain': 'getTimeDomain',
    'set_display_offset': 'setDisplayOffset',
    'commit_chunks': 'commitChunks',
    'enforce_retention': 'enforceRetention',
    'create_writer': 'createWriter',
    'create_reader': 'createReader',
    'list_datasets': 'listDatasets',
    'list_topics': 'listTopics',

    # query
    'for_each': 'forEach',
    'for_each_chunk': 'forEachChunk',
    'find_first_valid': 'findFirstValid',
    'skip_to_valid': 'skipToValid',
    'latest_at': 'latestAt',
    'range_query': 'rangeQuery',

    # writer
    'register_schema': 'registerSchema',
    'register_topic': 'registerTopic',
    'bind_topic_writer': 'bindTopicWriter',
    'resolve_field': 'resolveField',
    'append_columns': 'appendColumns',
    'register_scalar_series': 'registerScalarSeries',
    'append_scalar': 'appendScalar',
    'ensure_column': 'ensureColumn',
    'expand_array': 'expandArray',
    'flush_all': 'flushAll',
    'get_or_create_builder': 'getOrCreateBuilder',
    'ensure_cols_loaded': 'ensureColsLoaded',
    'build_column_descriptors': 'buildColumnDescriptors',
    'auto_seal': 'autoSeal',

    # reader
    'get_type_tree': 'getTypeTree',
    'get_metadata': 'getMetadata',

    # type_registry
    'register_or_get': 'registerOrGet',
    'find_by_name': 'findByName',
    'evolve_schema': 'evolveSchema',

    # arrow_import
    'schema_from_ipc': 'schemaFromIpc',
    'import_ipc_stream': 'importIpcStream',

    # derived_engine
    'output_kind': 'outputKind',
    'output_kinds': 'outputKinds',
    'add_siso_transform': 'addSisoTransform',
    'add_mimo_transform': 'addMimoTransform',
    'remove_node': 'removeNode',
    'has_node': 'hasNode',
    'output_topics': 'outputTopics',
    'topological_order': 'topologicalOrder',
    'on_source_committed': 'onSourceCommitted',

    # pj_plugins
    'apply_widget_data': 'applyWidgetData',
    'connect_widget_signals': 'connectWidgetSignals',
    'text_changed': 'textChanged',
    'index_changed': 'indexChanged',
    'value_changed': 'valueChanged',
    'selection_changed': 'selectionChanged',
    'file_selected': 'fileSelected',
    'tab_changed': 'tabChanged',
    'set_text': 'setText',
    'set_placeholder': 'setPlaceholder',
    'set_read_only': 'setReadOnly',
    'set_current_index': 'setCurrentIndex',
    'set_items': 'setItems',
    'set_checked': 'setChecked',
    'set_value': 'setValue',
    'set_range': 'setRange',
    'set_list_items': 'setListItems',
    'set_selected_items': 'setSelectedItems',
    'set_table_headers': 'setTableHeaders',
    'set_table_rows': 'setTableRows',
    'set_label': 'setLabel',
    'set_button_text': 'setButtonText',
    'set_file_picker': 'setFilePicker',
    'set_ok_enabled': 'setOkEnabled',
    'set_tab_index': 'setTabIndex',
    'set_enabled': 'setEnabled',
    'set_visible': 'setVisible',
    'to_json': 'toJson',
    'read_only': 'readOnly',
    'current_index': 'currentIndex',
    'value_int': 'valueInt',
    'value_double': 'valueDouble',
    'range_min': 'rangeMin',
    'range_max': 'rangeMax',
    'list_items': 'listItems',
    'selected_items': 'selectedItems',
    'table_headers': 'tableHeaders',
    'table_rows': 'tableRows',
    'button_text': 'buttonText',
    'is_file_picker': 'isFilePicker',
    'file_picker_filter': 'filePickerFilter',
    'file_picker_title': 'filePickerTitle',
    'ok_enabled': 'okEnabled',
    'tab_index': 'tabIndex',
    'widget_names': 'widgetNames',
    'has_widget': 'hasWidget',
    'get_string': 'getString',
    'get_int': 'getInt',
    'get_bool': 'getBool',
    'get_double': 'getDouble',
    'get_string_array': 'getStringArray',
    'send_event': 'sendEvent',
    'save_config': 'saveConfig',
    'load_config': 'loadConfig',
    'last_error': 'lastError',
    'safe_string': 'safeString',
    'current_text': 'currentText',
    'on_widget_event': 'onWidgetEvent',
    'on_tick': 'onTick',
    'on_accepted': 'onAccepted',
    'on_rejected': 'onRejected',
    'vtable_with_create': 'vtableWithCreate',
    'trampoline_destroy': 'trampolineDestroy',
    'trampoline_get_manifest': 'trampolineGetManifest',
    'trampoline_get_ui_content': 'trampolineGetUiContent',
    'trampoline_get_widget_data': 'trampolineGetWidgetData',
    'trampoline_on_widget_event': 'trampolineOnWidgetEvent',
    'trampoline_on_tick': 'trampolineOnTick',
    'trampoline_on_accepted': 'trampolineOnAccepted',
    'trampoline_on_rejected': 'trampolineOnRejected',
    'trampoline_save_config': 'trampolineSaveConfig',
    'trampoline_load_config': 'trampolineLoadConfig',
    'trampoline_get_last_error': 'trampolineGetLastError',
    'on_text_changed': 'onTextChanged',
    'on_index_changed': 'onIndexChanged',
    'on_toggled': 'onToggled',
    'on_value_changed': 'onValueChanged',
    'on_selection_changed': 'onSelectionChanged',
    'on_clicked': 'onClicked',
    'on_file_selected': 'onFileSelected',
    'on_tab_changed': 'onTabChanged',
    'show_dialog': 'showDialog',
    'run_headless': 'runHeadless',
    'last_stats': 'lastStats',
}

def rename_in_file(filepath):
    """Apply renames to function names in a file. Returns True if changed."""
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
    except Exception:
        return False

    original_content = content

    # Only rename actual function definitions and declarations
    # Use patterns that match function names but not when they appear in comments or string contexts
    for old_name, new_name in FUNCTION_RENAMES.items():
        # Match function/method declarations and definitions:
        # Pattern 1: "return_type old_name(" or "void old_name("
        pattern = r'(\s)(' + re.escape(old_name) + r')(\s*\()'
        content = re.sub(pattern, r'\1' + new_name + r'\3', content)

        # Pattern 2: "Class::old_name(" (method definitions)
        pattern = r'(::)(' + re.escape(old_name) + r')(\s*\()'
        content = re.sub(pattern, r'\1' + new_name + r'\3', content)

        # Pattern 3: "obj.old_name(" or "ptr->old_name(" (method calls)
        pattern = r'(\.)(' + re.escape(old_name) + r')(\s*\()'
        content = re.sub(pattern, r'\1' + new_name + r'\3', content)

        # Pattern 4: "->old_name(" (method calls through pointer)
        pattern = r'(->)(' + re.escape(old_name) + r')(\s*\()'
        content = re.sub(pattern, r'\1' + new_name + r'\3', content)

        # Pattern 5: function/method names used without calls (references, function pointers, etc.)
        # But only if surrounded by whitespace or punctuation
        pattern = r'\b(' + re.escape(old_name) + r')\b(?=\s*[{,;:)])'
        content = re.sub(pattern, new_name, content)

    if content != original_content:
        try:
            with open(filepath, 'w', encoding='utf-8') as f:
                f.write(content)
            return True
        except Exception:
            return False

    return False

def main():
    """Scan and rename in all source/header files."""
    project_root = Path('.')

    extensions = ['.cpp', '.hpp', '.h']
    files_to_process = []

    for ext in extensions:
        files_to_process.extend(project_root.rglob(f'*{ext}'))

    print(f"Found {len(files_to_process)} source/header files")

    changed_files = []
    for filepath in sorted(files_to_process):
        # Skip build directory and external files
        if '/build/' in str(filepath) or '/.cache/' in str(filepath):
            continue

        if rename_in_file(filepath):
            changed_files.append(filepath)
            print(f"  CHANGED: {filepath.relative_to(project_root)}")

    print(f"\n=== SUMMARY ===")
    print(f"Files processed: {len([f for f in files_to_process if '/build/' not in str(f)])}")
    print(f"Files changed: {len(changed_files)}")

if __name__ == '__main__':
    main()
