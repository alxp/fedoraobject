<?php

/**
 * @file 
 * Controller implementation for Fedora Commons repository objects.
 */

class FedoraObjectController extends DrupalDefaultEntityController {
  public function create($type = '', $pid = '', $title = '') {
    return (object) array(
      'foid' => '',
      'pid' => $pid,
      'type' => $type,
      'title' => $title,
    );
  }
  
  public function delete($foids) {
    if (!empty($foids)) {
      $fedoraobjects = $this->load($foids, array());
      $transaction = db_transaction();
      
      try {
        db_delete('fedoraobject')
          ->condition('foid', $foids, 'IN')
          ->execute();
        foreach($fedoraobjects as $fedoraobject_foid => $fedoraobject) {
          field_attach_delete('fedoraobject', $fedoraobject);
        }
        
        db_ignore_slave();
      }
      catch(Excaption $e) {
        $transaction->rollback();
        watchdog_exception('fedoraobject', $e, NULL, WATCHDOG_ERROR);
        return FALSE;
      }
      
      module_invoke_all('entity_delete', $fedoraobject, 'fedoraobject');
      
      // Clear the page, block and fedoraobject caches.
      cache_clear_all();
      $this->resetCache();
    }
    
    return TRUE;
  }
  
  public function load($ids = array(), $conditions = array()) {
    module_load_include('module', 'fedora_api');
    // Get the foids for any $ids that are PID strings.
    $pids = array();
    $foids = array();
    $ids_to_remove = array();
    $new_fedoraobjects = array();
    for ($i = 0; $i < count($ids); $i++) {
      if (!is_numeric($ids[$i]) && fedora_api_valid_pid($ids[$i])) {
        $pids[] = $ids[$i];
        $ids_to_remove[] = $i;
      }
    }
    
    foreach($ids_to_remove as $id) {
      unset($ids[$id]);
    }

    if (!empty($pids)) {
      $found_pids = array();
      // Get the objects by PID
      $select = db_select('fedoraobject', 'e')
                      ->fields('e', array('foid', 'pid'))
                      ->condition('pid', $pids, 'IN')
                      ->execute();
      while ($res = $select->fetchAssoc()) {
        $foids[] = $res['foid'];
        $found_pids[] = $res['pid'];
      }
      $remaining_pids = array_diff($pids, $found_pids);
      $new_foids = array();
      
      foreach ($remaining_pids as $remaining_pid) {
        $new_fedoraobjects[] = $this->createFedoraObjectForPid($remaining_pid);
      }
      
      $ids = array_merge($ids, $foids);
    }
    
    return array_merge(parent::load($ids, $conditions), $new_fedoraobjects);
  }
  
  public function save($fedoraobject) {
   $transaction = db_transaction();
    try {
      global $user;
      
      // Determine if we are inserting a new fedora object.
      $fedoraobject->is_new = empty($fedoraobject->foid);
      
      // Set the timestamp fields
      if (empty($fedoraobject->created)) {
        $fedoraobject->created = REQUEST_TIME;
      }
      $fedoraobject->changed = REQUEST_TIME;
      
      $fedoraobject->revision_timestamp = REQUEST_TIME;
      $update_fedoraobject = TRUE;
      
      // Give modules the opportunity to prepare field data for saving.
      field_attach_presave('fedoraobject', $fedoraobject);
      
      if (!$fedoraobject->is_new && !empty($fedoraobject->revision) && $fedoraobject->vid) {
        $fedoraobject->old_vid = $fedoraobject->vid;
        unset($fedoraobject->vid);
      }
      
      // If this is a new fedora object
      if ($fedoraobject->is_new) {
        // Save the new fedora object
        drupal_write_record('fedoraobject', $fedoraobject);
        
        // Save the initial revision
        $this->saveRevision($fedoraobject, $user->uid);
        $op = 'insert';
      }
      else {
        // Save the updated fedora object.
        drupal_write_record('fedoraobject', $fedoraobject, 'foid');
        if (!empty($fedoraobject->revision)) {
          $this->saveRevision($fedoraobject, $user->uid);
        }
        else {
          $this->saveRevision($fedoraobject, $user->uid, TRUE);
          $update_fedoraobject = TRUE;
        }
        
        $op = 'update';
      }
      
      // If the revision id is new or updated, save it to the fedora object.
      if ($update_fedoraobject) {
        db_update('fedoraobject')
          ->fields(array('vid' => $fedoraobject->vid))
          ->condition('foid', $fedoraobject->foid)
          ->execute();
      }
      // Save fields.
      $function = 'field_attach_' . $op;
      $function('fedoraobject', $fedoraobject);
      module_invoke_all('entity_' . $op, $fedoraobject, 'fedoraobject');
      
      // Clear internal properties
      unset($fedoraobject->is_new);
      
      // Ignore slave server temporarily to give time for the save order to be propagated to the slave.
      db_ignore_slave();
      
      return $fedoraobject;
    }
    catch(Exception $e) {
      $transaction->rollback();
      watchdog_exception('fedoraobject', $e, NULL, WATCHDOG_ERROR);
      return FALSE;
    }
  }
  
  function saveRevision($fedoraobject, $uid, $update = FALSE) {
    // Hold on to the original fedora object's creator_uid but swap
    // but swap in the revision's creator_uid for the momentary update
    $temp_uid = $fedoraobject->uid;
    $fedoraobject->uid = $uid;
    
    if ($update) {
      drupal_write_record('fedoraobject_revision', $fedoraobject, 'vid');
    }
    else {
      drupal_write_record('fedoraobject_revision', $fedoraobject);
    }
    
    // Reset the fedora object's creator_uid to the original value.
    $fedoraobject->uid = $temp_uid;
  }
  
  protected function createFedoraObjectForPid($pid) {
    module_load_include('inc', 'fedora_api', 'fedora_api.client');
    $fedora_client = new FedoraClient();
    $results = $fedora_client->findObjects(NULL, "pid=$pid");
    
    $new_fedoraobject = NULL;
    if (isset($results[$pid])) {
      $result = $results[$pid];
      $new_fedoraobject = $this->create('generic', $pid, $result['title']);
    }
    
    return $new_fedoraobject;
  }
}
