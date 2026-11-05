# ADR 0001: separate airfield controls and free loadout

Status: implemented candidate; runtime acceptance pending.

The 2.1.x helper converted a first waypoint near an island into a landing order.
Its three-kilometre world-space radius prevented useful ground orders, and its
hub-derived icon did not identify the actual airfield.

Use explicit icon/menu landing, real runway snapshots, four bay selections and
normal destination orders for launch. Keep stock/Revolution order call sites
unchanged. Wrap only the owned visible map interaction in the appended helper.
Retain the corrected 2.1.2 host flight director.

The owner includes loadout changes in this minor release and explicitly defers
inventory accounting. Create compatible attachments free and full through a
separate shared SDK equipment operation. The mod owns island/bay permissions;
the SDK owns native construction, compatibility and notifications. Do not
impersonate a carrier bay, install a private gameplay DLL or enable remote
mailbox mutations. Resource accounting remains a separate backlog item.

See [UI contract and acceptance](../AIRFIELD_UI_2_2.md).
