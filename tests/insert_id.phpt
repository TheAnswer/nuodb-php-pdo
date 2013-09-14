--TEST--
Test last insert id
--FILE--
<?php 

require("testdb.inc");
global $db;  
open_db();

// auto commit disabled tests with commits and rollback.
echo "Last insert id test.\n";

try {  

  $sql1 = "DROP TABLE INSERT_ID_TEST CASCADE IF EXISTS";
  echo $sql1 . "\n";
  $db->query($sql1);

  $sql2 = "CREATE TABLE INSERT_ID_TEST (ID Integer not NULL generated by default as identity primary key, A char)";
  echo $sql2 . "\n";
  $db->query($sql2);
} catch(PDOException $e) {  
  echo $e->getMessage() . "\n";  
}

try {  
  echo "Test 'No generated keys' exception\n";
  $ins_id = $db->lastInsertId();
} catch(PDOException $e) {  
  echo $e->getMessage() . "\n";  
}

try {  
  $sql3 = "INSERT INSERT_ID_TEST (A) VALUES('b')";
  echo $sql3 . "\n";
  $sth = $db->query($sql3);
  $affected_rows = $sth->rowCount();
  if ($affected_rows != 1) {
     echo "ERROR: affected_rows != 1\n";
  }
  $ins_id3 = $db->lastInsertId();
//  echo "ins_id3 = " . $ins_id3 . "\n"; 
  $sth = NULL;

  $sql4 = "INSERT INSERT_ID_TEST (A) VALUES('c')";
  echo $sql4 . "\n";
  $sth = $db->prepare($sql4);
  $affected_rows = $sth->execute();  
  if ($affected_rows != 1) {
     echo "ERROR: affected_rows != 1\n";
  }
  $ins_id4 = $db->lastInsertId();
//  echo "ins_id4 = " . $ins_id4 . "\n"; 
  $sth = NULL;
  if (($ins_id3 + 1) != $ins_id4) 
     echo "ERROR: ID NOT INCREMENTED\n"; 

  $sql5 = "INSERT INSERT_ID_TEST (A) VALUES('d')";
  echo $sql5 . "\n";
  $sth = $db->query($sql5);
  $affected_rows = $sth->rowCount();
  if ($affected_rows != 1) {
     echo "ERROR: affected_rows != 1\n";
  }
  $sth = NULL;
  $ins_id5 = $db->lastInsertId();
  if (($ins_id4 + 1) != $ins_id5) 
     echo "ERROR: ID NOT INCREMENTED\n"; 

  $sql6 = "INSERT INSERT_ID_TEST (A) VALUES('e')";
  echo $sql6 . "\n";
  $sth = $db->prepare($sql4);
  $affected_rows = $sth->execute();  
  if ($affected_rows != 1) {
     echo "ERROR: affected_rows != 1\n";
  }
  $sth = NULL;
  $ins_id6 = $db->lastInsertId();
  if (($ins_id5 + 1) != $ins_id6) 
     echo "ERROR: ID NOT INCREMENTED\n"; 

  $sql7 = "INSERT INSERT_ID_TEST (A) VALUES('f')";
  echo $sql7 . "\n";
  $sth = $db->query($sql7);
  $affected_rows = $sth->rowCount();
  if ($affected_rows != 1) {
     echo "ERROR: affected_rows != 1\n";
  }
  $sth = NULL;
  $ins_id7 = $db->lastInsertId();
  if (($ins_id6 + 1) != $ins_id7) 
     echo "ERROR: ID NOT INCREMENTED\n"; 

  $sql8 = "INSERT INSERT_ID_TEST (A) VALUES('g')";
  echo $sql8 . "\n";
  $sth = $db->prepare($sql8);
  $sth->execute();  
  $sth = NULL;
  $ins_id8 = $db->lastInsertId();
  if (($ins_id7 + 1) != $ins_id8) 
     echo "ERROR: ID NOT INCREMENTED\n"; 
} catch(PDOException $e) {  
  echo $e->getMessage() . "\n";  
}

try {
  echo "Test 'getLastId sequence-name argument is not supported' exception\n";
  $ins_id = $db->lastInsertId("no-such-sequence");
} catch(PDOException $e) {  
  echo $e->getMessage() . "\n";  
}
  
try {
  $sql = "DROP TABLE INSERT_ID_TEST CASCADE IF EXISTS";
  echo $sql . "\n";
  $db->query($sql);
} catch(PDOException $e) {  
  echo $e->getMessage() . "\n";  
}

$db = NULL;  

echo "done\n";
?>
--EXPECT--
Last insert id test.
DROP TABLE INSERT_ID_TEST CASCADE IF EXISTS
CREATE TABLE INSERT_ID_TEST (ID Integer not NULL generated by default as identity primary key, A char)
Test 'No generated keys' exception
SQLSTATE[HY000] [-40] No generated keys
INSERT INSERT_ID_TEST (A) VALUES('b')
INSERT INSERT_ID_TEST (A) VALUES('c')
INSERT INSERT_ID_TEST (A) VALUES('d')
INSERT INSERT_ID_TEST (A) VALUES('e')
INSERT INSERT_ID_TEST (A) VALUES('f')
INSERT INSERT_ID_TEST (A) VALUES('g')
Test 'getLastId sequence-name argument is not supported' exception
SQLSTATE[IM001] [1] getLastId sequence-name argument is not supported
DROP TABLE INSERT_ID_TEST CASCADE IF EXISTS
done
