<?php
/**
 * @file
 * user_fields.features.inc
 */

/**
 * Implements hook_ctools_plugin_api().
 */
function user_fields_ctools_plugin_api() {
  list($module, $api) = func_get_args();
  if ($module == "field_group" && $api == "field_group") {
    return array("version" => "1");
  }
}
