<?php
/**
 * @file
 * kms_oci_queue_job.inc.
 * User: mikkel@adapt.dk
 * Date: 11/16/13 - 12:16 PM
 */

class KmsOciQueueJob {

  const STATUS_WAITING = 0;
  const STATUS_PROCESSING = 1;
  const STATUS_FILE_READY = 2;
  const STATUS_FAILED = 3;
  const STATUS_DONE = 4;

  public static $queues = array(
    'creator' => array(
      'name' => 'kms_oci_queue_query_creator',
      'class' => 'QueryQueue',
    ),
    'executor' => array(
      'name' => 'kms_oci_queue_query',
      'class' => 'QueryQueue',
    ),
  );

  public $jid = 0;
  public $created;
  public $status;
  public $action;
  public $actionDetails;
  public $cid;
  protected $queueName;
  protected $job;
  public $gid = FALSE;

  public function __construct($jid) {
    $this->setJobId($jid);
    $this->setJob();
    if ($this->job) {
      $this->setGroupId();
      $this->setQueueName($this->loadQueueName());
      $this->setCreated();
      $this->setAction();
      $this->setActionDetails();
      $this->setStatus();
      $this->setConnectionId();
    }
  }

  protected function setJobId($jid){
    $this->jid = is_null($jid) || $jid == 'null' ? NULL : $jid;
  }

  protected function setJob(){
    $this->job = KmsOciQueueJobDb::loadJob($this->jid);
  }

  protected function setCreated(){
    $this->created = $this->job->created;
  }

  protected function setConnectionId(){
    $this->cid = $this->job->cid;
  }

  protected function setQueueName($queue_name){
    $this->queueName = $queue_name;
  }

  protected function setAction(){
    $this->action = $this->job->action;
  }

  protected function setGroupid(){
    $this->gid = KmsOciQueueJobGroup::getGidFromJid($this->jid);
  }

  protected function setActionDetails(){
    $action_details = unserialize($this->job->action_details);
    $this->actionDetails = kms_oci_queue_json_pretty(
      $action_details,
      array('format' => 'html')
    );
  }

  public function getLog($mode = 'all') {
    return $this->loadLog($mode);
  }

  protected function setStatus() {
    $this->status = (object) array(
      'status' => $this->job->status,
      'message' => $this->job->message,
    );
  }

  protected function loadLog($mode = 'all') {
    $query = db_select('kms_oci_queue_log', 'ql');
    $query->fields('ql');
    $query->condition('ql.jid', $this->jid, '=');
    $query->orderBy('lid', 'DESC');
    switch ($mode) {
      case 'latest':
        $query->range(0,1);
        break;
      case 'all':
        break;
    }
    $result = $query->execute()->fetchAllAssoc('lid');
    return $result ? $result : array();
  }

  public function loadQueueItem() {
    $query = db_select('queue', 'q');
    $query->fields('q');
    $query->condition('q.item_id', $this->jid, '=');
    $result = $query->execute()->fetchObject();

    if (is_object($result) && !empty($result->data)) {
      $result->data = unserialize($result->data);
    }
    return $result;
  }

  public function loadQueueName() {
    return db_query(
      'SELECT q.name FROM {queue} q WHERE q.item_id = :jid',
      array(':jid' => $this->jid)
    )->fetchField();
  }

  public function changeStatus($status, $message = '', $variables = array()) {
    $severity = WATCHDOG_INFO;
    switch ($status) {
      case self::STATUS_FAILED:
        $severity = WATCHDOG_ERROR;
        break;

      case self::STATUS_DONE:
        KmsOciQueueJobUserRelations::delete($this);
        break;

    }
    $job = $this->job;
    $job->status = $status;
    KmsOciQueueJobDb::updateJob($job);
    if (!empty($message)) {
      self::log($this->jid, $message, $variables, $severity);
    }
  }

  public function changeActionDetails($action_details) {
    $job = $this->job;
    $job->action_details = $action_details;
    KmsOciQueueJobDb::updateJob($job);
  }

  public static function create($params, $cid = 'default') {
    $params += array(
      'action' => '',
      'action_details' => '',
    );
    // If any hooks are blocking access do not create job.
    if (!kms_oci_queue_job_is_accessible($params['action'])) {
      return;
    }
    $queue = DrupalQueue::get(self::$queues['creator']['name'], TRUE);
    $job = new stdClass();
    $job->jid = $queue->createItem((object) $params);
    $job->status = KmsOciQueueJob::STATUS_WAITING;
    $job->created = time();
    $job->cid = $cid;
    $job->action = $params['action'];
    $job->action_details = serialize($params['action_details']);
    $job_saved = KmsOciQueueJobDb::insertJob($job);
    if ($job_saved) {
      self::log($job->jid, t('Job %jid created'), array('%jid' => $job->jid));
      $job = KmsOciQueueJobFactory::get($job->jid);
      // Find plugins of same action
      // and save job/user relations if the job does not have fixed permissions.
      ctools_include('plugins');
      $plugins = ctools_get_plugins('kms_oci_queue', 'queue_runner');
      foreach ($plugins as $plugin) {
        $plugin += array('fixed_permissions' => FALSE);
        if (
          $plugin['action'] == $job->action
            && $plugin['fixed_permissions'] == FALSE
              && (!empty($params['params']) && !empty($params['params']['uids']))
        ) {
          KmsOciQueueJobUserRelations::save($job, $params['params']['uids']);
        }
      }
      return $job;
    }
  }

  public function delete() {
    // Delete queue item;
    $queue = DrupalQueue::get($this->queueName);
    $queue->deleteItem((object) array('item_id' => $this->jid));
    // Delete job from job table.
    KmsOciQueueJobDb::deleteJob($this->jid);
    // Delete related log entries.
    db_delete('kms_oci_queue_log')
      ->condition('jid', $this->jid)
      ->execute();
    KmsOciQueueJobGroup::deleteByJob($this);
    KmsOciQueueJobUserRelations::delete($this);
  }

  public function finishQueue() {
    // Delete queue item;
    $queue = DrupalQueue::get($this->queueName);
    $queue->deleteItem((object) array('item_id' => $this->jid));
  }

  public static function log($jid, $message, $variables = array(), $severity = WATCHDOG_INFO) {
    global $user;

    $record = new stdClass();
    $record->jid = $jid;
    $record->uid = $user->uid;
    $record->message = $message;
    $record->variables = $variables;
    $record->severity = $severity;
    $record->timestamp = time();
    // Makes it possible to skip the kms_oci_queue_log
    // by setting the jid to 0 or FALSE;
    if ($jid) {
      drupal_write_record('kms_oci_queue_log', $record);
    }
    watchdog(
      implode(':', array(KMS_OCI_QUEUE_PREFIX, $jid)),
      $message,
      $variables,
      $severity
    );
  }

  public static function status2human($status) {
    $statuses = self::statuses();
    return $statuses[$status];
  }

  protected static function statuses() {
    return array(
      self::STATUS_WAITING => t('Waiting'),
      self::STATUS_PROCESSING => t('Processing'),
      self::STATUS_FILE_READY  => t('Job file ready'),
      self::STATUS_FAILED => t('Failed'),
      self::STATUS_DONE => t('Done'),
    );
  }

  public static function cssClassSeverity($severity) {
    $map = array(
      WATCHDOG_DEBUG     => 'kms-oci-queue-severity-debug',
      WATCHDOG_INFO      => 'kms-oci-queue-severity-info',
      WATCHDOG_NOTICE    => 'kms-oci-queue-severity-notice',
      WATCHDOG_WARNING   => 'kms-oci-queue-severity-warning',
      WATCHDOG_ERROR     => 'kms-oci-queue-severity-error',
      WATCHDOG_CRITICAL  => 'kms-oci-queue-severity-critical',
      WATCHDOG_ALERT     => 'kms-oci-queue-severity-alert',
      WATCHDOG_EMERGENCY => 'kms-oci-queue-severity-emerg',
    );
    return !empty($map[$severity]) ? $map[$severity] : '';
  }

  public static function cssClassStatus($status) {
    $map = array(
      self::STATUS_WAITING     => 'kms-oci-queue-status-waiting',
      self::STATUS_PROCESSING  => 'kms-oci-queue-status-processing',
      self::STATUS_FILE_READY  => 'kms-oci-queue-status-file-ready',
      self::STATUS_FAILED      => 'kms-oci-queue-status-failed',
      self::STATUS_DONE        => 'kms-oci-queue-status-done',
    );
    return !empty($map[$status]) ? $map[$status] : '';
  }

}

class KmsOciQueueJobDb {

  const schema = "kms_oci_queue_job";

  public static function insertJob($job){
    $job->timestamp = time();
    return drupal_write_record(self::schema, $job);
  }

  public static function updateJob($job){
    return drupal_write_record(self::schema, $job, 'jid');
  }

  public static function loadJob($jid){
    $query = db_select('kms_oci_queue_job', 'j');
    $query->fields('j');
    $query->condition('j.jid', $jid, '=');
    $result = $query->execute()->fetchObject();
    if ($result) {
      $result->message = KmsOciQueueJob::status2human($result->status);
    }
    return $result;
  }

  public static function deleteJob($jid){
    db_delete(self::schema)
      ->condition('jid', $jid)
      ->execute();
  }

  public static function getConnectionSettings($cid) {
    switch ($cid) {
      case 'kms_permissions':
        return array(
          'user' => variable_get('kms_permissions_conn_user', ''),
          'pass' => variable_get('kms_permissions_conn_pass', ''),
          'host' => variable_get('kms_permissions_conn_host', ''),
          'db' => variable_get('kms_permissions_conn_db', ''),
        );
        break;
      default:
        return array(
          'user' => variable_get('kms_oci_conn_user', ''),
          'pass' => variable_get('kms_oci_conn_pass', ''),
          'host' => variable_get('kms_oci_conn_host', ''),
          'db' => variable_get('kms_oci_conn_db', ''),
        );
        break;
    }
  }

}

class KmsOciQueueJobGroup {

  const schema = "kms_oci_queue_job_group";

  protected $gid = 0;

  public function __construct() {
    $this->gid = uniqid();
  }

  public function add($job) {
    $jobs = is_array($job) ? $job : array($job);
    // We only want jobs that actually are jobs :).
    $jobs = array_filter($jobs);
    foreach ($jobs as $job) {
      $this->insertJob($job);
    }
  }

  protected function insertJob(KmsOciQueueJob $job){
    $group_job = new stdClass();
    $group_job->gid = $this->gid;
    $group_job->jid = $job->jid;
    drupal_write_record(self::schema, $group_job);
    return $group_job->jid;
  }

  public static function getGidFromJid($jid) {
    $query = db_select(self::schema, 'g');
    $query->fields('g', array('gid'));
    $query->condition('g.jid', $jid, '=');
    $result = $query->execute()->fetchField();
    return $result;
  }

  public static function getJidsFromGid($gid) {
    $query = db_select(self::schema, 'g');
    $query->fields('g', array('jid'));
    $query->condition('g.gid', $gid, '=');
    return $query->execute()->fetchCol();
  }

  public static function deleteByJob(KmsOciQueueJob $job) {
    db_delete(self::schema)
      ->condition('jid', $job->jid)
      ->execute();
  }

  public static function getJobWeight($job) {
    if (!$job->gid) {
      return FALSE;
    }
    $jids = KmsOciQueueJobGroup::getJidsFromGid($job->gid);
    if (!$jids) {
      return FALSE;
    }
    $gids = array_flip($jids);
    return is_int($gids[$job->jid]) ? $gids[$job->jid] : FALSE;
  }

  public static function cssClassJobGroup($job, $type) {
    $class = '';
    if ($job->gid) {
      $class = 'kms-oci-queue-job-group';
      $job_count = count(self::getJidsFromGid($job->gid));
      $job_weight = self::getJobWeight($job);
      if ($job_weight !== FALSE) {
        switch ($type) {
          case 'jobs_waiting':
            if ($job_weight == 0) {
              $class .= '-first';
            }
            elseif ($job_weight == ($job_count - 1)) {
              $class .= '-last';
            }
            break;
          case 'jobs_done':
            if ($job_weight == 0) {
              $class .= '-last';
            }
            elseif ($job_weight == ($job_count - 1)) {
              $class .= '-first';
            }
            break;
        }
      }
    }
    return $class;
  }

}


class KmsOciQueueJobFactory {
  protected static $jobs;

  public static function get($jid) {

    if (!isset($jobs[$jid])) {
      $job = new KmsOciQueueJob($jid);
      // If both status and created are not populated
      // the job object is not valid.
      if (empty($job->status) && empty($job->created)) {
        $job = FALSE;
      }

      self::$jobs[$jid] = $job;
    }

    return self::$jobs[$jid];
  }

}