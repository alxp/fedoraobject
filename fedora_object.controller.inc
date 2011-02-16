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
      $fedora_objects = $this->load($pids, array());
      $transaction = db_transaction();
      
      try {
        db_delete('fedora_object')
          ->condition('pid', $pids, 'IN')
          ->execute();
        foreach($fedora_objects as $fedora_object_pid => $fedora_object) {
          field_attach_delete('fedora_object', $fedora_object);
        }
        
        db_ignore_slave();
      }
      catch(Excaption $e) {
        $transaction->rollback();
        watchdog_exception('fedora_object', $e, NULL, WATCHDOG_ERROR);
        return FALSE;
      }
      
      module_invoke_all('entity_delete', $fedora_object, 'fedora_object');
      
      // Clear the page, block and fedora_object caches.
      cache_clear_all();
      $this->resetCache();
    }
    
    return TRUE;
  }
  
  public function save($fedora_object) {
    $transaction = db_transaction();
    
    try {
      global $user;
      
      // Determine if we are inserting a new fedora object.
      $fedora_object->is_new = empty($fedora_object->pid);
      
      // Set the timestamp fields
      if (empty($fedora_object->created)) {
        $fedora_object->created = REQUEST_TIME;
      }
      $fedora_object->changed = REQUEST_TIME;
      
      $fedora_object->revision_timestamp = REQUEST_TIME;
      $update_fedora_object = TRUE;
      
      // Give modules the opportunity to prepare field data for saving.
      field_attach_presave('fedora_object', $fedora_object);
      
      if (!$fedora_object->is_new && !empty($fedora_object->revision) && $fedora_object->vid) {
        $fedora_object->old_vid = $fedora_object->vid;
        unset($fedora_object->vid);
      }
      
      // If this is a new artwork
      if ($fedora_object->is_new) {
        // Save the new fedora object
        drupal_write_record('fedora_object', $fedora_object);
        
        // Save the initial revision
        $this->saveRevision($fedora_object, $user->uid);
        $op = 'insert';
      }
      else {
        // Save the updated fedora object.
        drupal_write_record('fedora_object', $fedora_object, 'pid');
        if (!empty($fedora_object->revision)) {
          $this->saveRevision($fedora_object, $user->uid);
        }
        else {
          $this->saveRevision($fedora_object, $user->uid, TRUE);
          $update_fedora_object = TRUE;
        }
        
        $op = 'update';
      }
      
      // If the revision id is new or updated, save it to the fedora object.
      if ($update_fedora_object) {
        db_update('fedora_object')
          ->fields(array('vid'), $fedora_object->pid)
          ->condition('pid', $fedora_object->pid)
          ->execute();
      }
      // Save fields.
      $function = 'field_attach_' . $op;
      $function('fedora_object', $fedora_object);
      module_invoke_all('entity_' . $op, $fedora_object, 'fedora_object');
      
      // Clear internal properties
      unset($fedora_object->is_new);
      
      // Ignore slave server temporarily to give time for the save order to be propagated to the slave.
      db_ignore_slave();
      
      return $fedora_object;
    }
    catch(Exception $e) {
      $transaction->rollback();
      watchdog_exception('fedora_object', $e, NULL, WATCHDOG_ERROR);
      return FALSE;
    }
  }
  
  function saveRevision($fedora_object, $uid, $update = FALSE) {
    // Hold on to the original artwork's creator_uid but swap
    // but swap in the revision's creator_uid for the momentary update
    $temp_uid = $fedora_object->uid;
    $fedora_object->uid = $uid;
    
    if ($update) {
      drupal_write_record('fedora_object_revision', $fedora_object, 'vid');
    }
    else {
      drupal_write_record('fedora_object_revision', $fedora_object);
    }
    
    // Reset the fedora object's creator_uid to the original value.
    $fedora_object->uid = $temp_uid;
  }
}

function fedora_object_load($pid = NULL, $vid = NULL, $reset = FALSE) {
  $pids = (isset($pid) ? array($pid) : array());
  $conditions = (isset($vid) ? array('vid' => $vid) : array());
  $fedora_object = fedora_object_load_multiple($pids, $conditions, $reset);
  return $fedora_object ? reset($fedora_object) : FALSE;
}

function fedora_object_load_multiple($pids = array(), $conditions = array(), $reset = FALSE) {
  return entity_load('fedora_object', $pids, $conditions, $reset);
}

