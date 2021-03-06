<?php
/**
 * @file
 * kms_permissions_bounding_box_update.inc
 *
 * Provides callback functions run by the queue handling functions.
 * Creates oracle queries with permissions for all the involved users.
 */

$plugin = array(
  'action' => KMS_OCI_QUEUE_ACTION_BOUNDING_BOX_UPDATE,
  'query_creator_run' => array(
    'function' => 'kms_permissions_bounding_box_update_query_creator_run',
  ),
  'query_run' => array(
    'function' => 'kms_permissions_bounding_box_update_query_run',
  ),
);

/**
 * Query creator run function.
 *
 * @param int $jid
 *   Kms oci queue job id.
 * @param object $creator_item
 *   Queue item.
 * @param object $exe_queue
 *   Execute queue that is being populated with items.
 *
 * @return int
 *   Number of query lines to be written.
 */
function kms_permissions_bounding_box_update_query_creator_run($jid, $creator_item, $exe_queue) {
  $job = KmsOciQueueJobFactory::get($jid);
  $bid = $creator_item->params['bid'];
  $bounding_box_diff = $creator_item->params['bounding_box_diff'];

  $perms = array();
  _kms_permissions_get_permissions_by_bid(
    $bid,
    $perms,
    array('key_prefix' => FALSE, 'flattened' => TRUE)
  );

  // If no services are present make sure to end this thing.
  if (empty($perms['services'])) {
    $exe_queue_item = array(
      'jid' => $job->jid,
      'sid' => 0,
      'action' => $creator_item->action,
      'data' => array(),
      'is_last_item' => TRUE,
    );
    $exe_queue->createItem((object) $exe_queue_item);
    return;
  }

  $service_count = count($perms['services']);
  $i = 0;
  foreach ($perms['services'] as $sid) {
    $i++;
    $is_last_item = $i == $service_count;
    $service_bounding_box = kms_permissions_get_service_bounding_box($sid);
    $data = array();
    foreach ($service_bounding_box as $param => $service_value) {
      if (empty($bounding_box_diff[$param])) {
        continue;
      }
      $data[$param] = $service_value;
    }

    if (!empty($data)) {
      $data['serviceid'] = $sid;
      // Queue the item.
      $exe_queue_item = array(
        'jid' => $job->jid,
        'sid' => $sid,
        'action' => $creator_item->action,
        'data' => $data,
        'is_last_item' => $is_last_item,
      );
      $exe_queue->createItem((object) $exe_queue_item);
    }
  }

  return $i;
}

/**
 * Query run function.
 *
 * Creates query queue items ready to be written in an job file.
 *
 * @param object $item
 *   Queue item.
 *
 * @return string
 *   Generated oracle queries.
 */
function kms_permissions_bounding_box_update_query_run($item) {
  $sid = $item->sid;
  $data = $item->data;
  $output = '';
  if (!empty($data)) {
    $wms_ids = kms_permissions_get_service_ids_by_type('wms');
    if (in_array($sid, $wms_ids)) {
      $data['featureinfo'] = 1;
    }
    $sql = new KmsOciQueueSql('services', $data);
    $output = $sql->generate('updateBySid', $data);
  }

  return $output;
}
