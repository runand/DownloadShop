<?php
/**
 * @file
 * kms_oci_queue_sql.inc.
 * User: mikkel@adapt.dk
 * Date: 03/20/14 - 13:07 PM
 */

class KmsOciQueueSql {

  private $type = '';
  private $data = array();
  private $sqlClass = '';
  private $conf = array();

  public function __construct($type, $data) {
    $this->type = $type;
    $this->data = $data;
    $this->sqlClass = sprintf('KmsOciQueueSql%s', ucfirst($type));
    $conf_callback = array($this->sqlClass, 'conf');
    if (is_callable($conf_callback)) {
      $this->conf = call_user_func($conf_callback);
    }
  }

  public function generate($query_type) {
    $callback = array($this->sqlClass, $query_type);
    if (is_callable($callback)) {
      $args = func_get_args();
      $args = array_slice($args, 1);
      $sql = call_user_func_array($callback, $args);
      $sql = $this->replacePlaceholders($sql, $this->data);
      return !empty($sql) ? sprintf("%s\n", $sql) : FALSE;
    }
  }

  public function generateHeader() {
    $callback = array($this->sqlClass, 'header');
    if (is_callable($callback)) {
      $sql = call_user_func_array($callback, func_get_args());
      $sql = $this->replacePlaceholders($sql, $this->data);
      return !empty($sql) ? $sql : FALSE;
    }
  }

  protected function replacePlaceholders($sql, $data) {
    $patterns = $replacements = array();
    array_walk($data, function($v, $k) use(&$patterns, &$replacements){
      $patterns[] = '#\\*' . $k . '\\*#';
      $replacements[] = "'$v'";
    });
    return preg_replace($patterns, $replacements, $sql);
  }

}

class KmsOciQueueSqlServices {

  public static $conf = array(
    'table' => 'USERRESTRICTIONS2',
    'cid' => 'kms_permissions',
    'default_values' => array(
      'SERVICEID' => '',
      'MINX' => '',
      'MINY' => '',
      'MAXX' => '',
      'MAXY' => '',
      'MAXPIXELWIDTH' => '',
      'MAXPIXELHEIGHT' => '',
      'FEATUREINFO' => '0',
    ),
  );

  public static function header() {
    return <<<EOD
var userid varchar2(50);
exec :userid:=*userid*;
EOD;
  }

  public static function upsert() {
    $table = self::$conf['table'];
    return <<<EOD
MERGE INTO $table t
USING dual ON (t.userid = :userid AND t.serviceid = *serviceid*)
WHEN NOT MATCHED THEN
  INSERT (SERVICEID, USERID, MINX, MINY, MAXX, MAXY, MAXPIXELWIDTH, MAXPIXELHEIGHT, FEATUREINFO)
  VALUES (*serviceid*, :userid, *minx*, *miny*, *maxx*, *maxy*, *maxpixelwidth*, *maxpixelheight*, *featureinfo*)
WHEN MATCHED THEN
  UPDATE SET t.MINX = *minx*, t.MINY = *miny*, t.MAXX = *maxx*, t.MAXY = *maxy*, t.MAXPIXELWIDTH = *maxpixelwidth*, t.MAXPIXELHEIGHT = *maxpixelheight*, t.FEATUREINFO = *featureinfo*
;
EOD;
  }

  public static function insert() {
    $table = self::$conf['table'];
    return <<<EOD
INSERT INTO $table (SERVICEID, USERID, MINX, MINY, MAXX, MAXY, MAXPIXELWIDTH, MAXPIXELHEIGHT, FEATUREINFO)
VALUES (*serviceid*, :userid, *minx*, *miny*, *maxx*, *maxy*, *maxpixelwidth*, *maxpixelheight*, *featureinfo*)
;
EOD;
  }

  public static function delete() {
    return sprintf(
      'DELETE FROM %s t WHERE t.userid = :userid AND t.serviceid = *serviceid*;',
      self::$conf['table']
    );
  }

}

class KmsOciQueueSqlApplications {

  public static $conf = array(
    'table' => 'SMUSERGROUP',
    'cid' => 'default',
    'default_values' => array(
      'USERID' => '',
      'GROUPID' => '',
    ),
  );

  public static function header() {
    return <<<EOD
var userid varchar2(50);
exec :userid:=*userid*;
EOD;
  }

  public static function upsert() {
    $table = self::$conf['table'];
    return <<<EOD
MERGE INTO $table t
USING dual ON (t.userid = :userid AND t.groupid = *groupid*)
WHEN NOT MATCHED THEN
  INSERT (GROUPID, USERID)
  VALUES (*groupid*, :userid)
;
EOD;
  }

  public static function insert() {
    $table = self::$conf['table'];
    return <<<EOD
INSERT INTO $table (ID, GROUPID, USERID) VALUES (:userid, *groupid*, :userid);
EOD;
  }

  public static function delete() {
    return sprintf(
      'DELETE FROM %s t WHERE t.userid = :userid AND t.groupid = *groupid*;',
      self::$conf['table']
    );
  }

}

class KmsOciQueueSqlUser {

  public static $conf = array(
    'tables' => array('SMUSER', 'SMUSER_EXTRA'),
    'cid' => 'kms_permissions',
    'default_values' => array(
      'SMUSER' => array(
        'USERID' => '',
        'NAME' => '',
        'EMAILADDRESS' => '',
        'DISABLED' => '',
        'FIRSTNAME' => '',
        'LASTNAME' => '',
        'TELEPHONENUMBER' => '',
        'DEBITORNO' => '',
        'PASSWORD' => '',
      ),
      'SMUSER_EXTRA' => array(
        'USERID' => '',
        'DUID' => '',
        'USERTYPE' => '',
        'USERTYPE_INFO' => '',
        'RECEIVE_NEWSLETTER' => '',
        'ACCEPT_TERMS' => '',
        'CONTACT_ME' => '',
      ),
    ),
  );

  public static function header() {
    return <<<EOD
var userid varchar2(50);
exec :userid:=*userid*;
EOD;
  }

  public static function upsert($table = '') {
    $sql = array();
    $tables = self::$conf['default_values'];
    foreach ($tables as $table_name => $conf) {
      if (!empty($table) && strtoupper($table) != $table_name) {
        continue;
      }
      $cols = array_keys($conf);
      $sql_data = self::sqlData($cols);
      $insert_cols = implode(', ', $cols);
      $insert_placeholders = implode(', ', $sql_data['insert']);
      $update_placeholders = implode(', ', $sql_data['update']);
      $sql[] = <<<EOD
MERGE INTO $table t
USING dual ON (t.userid = :userid)
WHEN NOT MATCHED THEN
  INSERT ($insert_cols)
  VALUES ($insert_placeholders)
WHEN MATCHED THEN
  UPDATE SET $update_placeholders
;
EOD;
    }
    $output = implode("\n", $sql);
    return $output ? $output : '';
  }

  public static function insert($table = '') {
    $sql = array();
    $tables = self::$conf['default_values'];
    foreach ($tables as $table_name => $conf) {
      if (!empty($table) && strtoupper($table) != $table_name) {
        continue;
      }
      $cols = array_keys($conf);
      $sql_data = self::sqlData($cols);
      $insert_cols = implode(', ', $cols);
      $insert_placeholders = implode(', ', $sql_data['insert']);
      $sql[] = <<<EOD
INSERT INTO $table ($insert_cols) VALUES ($insert_placeholders);
EOD;
    }
    $output = implode("\n", $sql);
    return $output ? $output : '';
  }

  public static function update($table = '') {
    $sql = array();
    $tables = self::$conf['default_values'];
    foreach ($tables as $table_name => $conf) {
      if (!empty($table) && strtoupper($table) != $table_name) {
        continue;
      }
      $cols = array_keys($conf);
      $sql_data = self::sqlData($cols);
      $update_placeholders = implode(', ', $sql_data['update']);
      $sql[] = <<<EOD
UPDATE $table t SET $update_placeholders WHERE t.userid = :userid;
EOD;
    }
    $output = implode("\n", $sql);
    return $output ? $output : '';
  }

  protected static function sqlData($cols) {
    $data = array('insert', 'update');
    array_walk($cols, function($col) use(&$data) {
      $sql_col = strtolower($col);
      $data['insert'][] = $sql_col == 'userid' ? ':userid' : "*$sql_col*";
      if ($sql_col != 'userid') {
        $data['update'][] = "t.$sql_col = *$sql_col*";
      }
    });
    return $data;
  }

}