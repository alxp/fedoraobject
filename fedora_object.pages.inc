<?php

function fedora_object_add_page() {
  $item = menu_get_item();
  $links = system_admin_menu_block($item);
  foreach ($links as $link) {
    $items[] = l($link['title'], $link['href'], $item['localized_options'])
            . ':' . filter_xss_admin($link['description']);
  }
  return theme('item_list', array('items' => $items));
}