<?php

/**
 * @file 
 * Controller implementation for Fedora Commons repository objects.
 */

class FedoraObjectController extends DrupalDefaultEntityController {
  public function create($type = '') {
    return (object) array(
      'pid' => '',
      'type' => $type,
      'title' => '',
    );
  }
  
  public function delete($pids) {
    if (!empty($pids)) {
      $fedoraobjects = $this->load($pids, array());
      $transaction = db_transaction();
      
      try {
        db_delete('fedoraobject')
          ->condition('pid', $pids, 'IN')
          ->execute();
        foreach($fedoraobjects as $fedoraobject_pid => $fedoraobject) {
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
  
  public function save($fedoraobject) {
    $transaction = db_transaction();
    
    try {
      global $user;
      
      // Determine if we are inserting a new fedora object.
      $fedoraobject->is_new = empty($fedoraobject->pid);
      
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
      
      // If this is a new artwork
      if ($fedoraobject->is_new) {
        // Save the new fedora object
        drupal_write_record('fedoraobject', $fedoraobject);
        
        // Save the initial revision
        $this->saveRevision($fedoraobject, $user->uid);
        $op = 'insert';
      }
      else {
        // Save the updated fedora object.
        drupal_write_record('fedoraobject', $fedoraobject, 'pid');
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
          ->condition('pid', $fedoraobject->pid)
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
    // Hold on to the original artwork's creator_uid but swap
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
}

function fedoraobject_load($pid = NULL, $vid = NULL, $reset = FALSE) {
  $pids = (isset($pid) ? array($pid) : array());
  $conditions = (isset($vid) ? array('vid' => $vid) : array());
  $fedoraobject = fedoraobject_load_multiple($pids, $conditions, $reset);
  return $fedoraobject ? reset($fedoraobject) : FALSE;
}

function fedoraobject_load_multiple($pids = array(), $conditions = array(), $reset = FALSE) {
  return entity_load('fedoraobject', $pids, $conditions, $reset);
}
